#include "platform/rendering/mpv/mpvrenderitem.h"
#include "platform/rendering/mpv/mpvcontroller.h"

extern "C" {
#include <mpv/render_gl.h>
#ifdef Q_OS_WIN
#include <mpv/render_d3d11.h>
#endif
#if __has_include(<mpv/render_vulkan.h>)
#include <mpv/render_vulkan.h>
#else
// Fallback: mpv_vulkan_fbo struct (from render_vulkan.h). Must match the
// fork's header exactly — out_layout is written back by mpv (the layout the
// image is left in after rendering).
#define MPV_RENDER_PARAM_VULKAN_FBO ((mpv_render_param_type)25)
struct mpv_vulkan_fbo { void *image; int format, usage; int w, h; int out_layout; };
#endif
}

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QQuickWindow>
#include <QMutex>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#include <d3d11.h>
#endif
#include <rhi/qrhi_platform.h>
#if QT_CONFIG(vulkan)
#include <vulkan/vulkan.h>
#include <QVulkanInstance>
#include <QVulkanFunctions>
#include <QSGSimpleTextureNode>
#include <QtQuick/qsgtexture_platform.h>
#endif

namespace {
// Render-thread state shared between VideoRenderNode::prepare() and render().
// Anonymous namespace instead of file-scope static to limit linkage and clarify
// ownership. Kept as free variables (not MpvController members) because
// VideoRenderNode's lifetime is managed by Qt SceneGraph and may outlive any
// particular MpvController instance during teardown.

QMutex s_renderMutex;  // accessed via VideoRenderNode::renderMutex()

struct VideoRenderState {
    mpv_render_context *renderCtx = nullptr;
    bool hasVideo = false;
    bool fboReady = false;
    // True once mpv has rendered at least one frame into the cached offscreen
    // target (offFbo); reset when the target is recreated. While true, a
    // repaint with no new mpv frame can re-blit the cached content instead of
    // re-running the full mpv render.
    bool offValid = false;
    QSize nodeSize;
    MpvRenderItem *item = nullptr;
    // OpenGL cached offscreen FBO (avoids per-frame gen/delete, see #2)
    GLuint offFbo = 0;
    GLuint offTex = 0;
    QSize offFboSize;
};

QHash<VideoRenderNode *, VideoRenderState> s_state;
QMutex s_stateMutex;

#if QT_CONFIG(vulkan)
// ── Vulkan render API backend ──
//
// mpv renders into a privately owned VkImage; Qt Quick then samples it as an
// ordinary scene-graph texture. Everything runs in updatePaintNode() — on the
// render thread with the GUI thread blocked, outside Qt's render pass — so
// mpv's own queue submissions are serialized against Qt's without locking,
// and no commands are ever recorded inside Qt's render pass (blits and layout
// transitions there are invalid; the old blit-into-rendertarget path did
// exactly that, plus the blit destination lacked TRANSFER_DST usage).
//
// Qt learns the image's current layout via QRhiTexture::setNativeLayout()
// after every mpv render and records the transition to SHADER_READ_ONLY in
// its own pass — the same mechanism QtMultimedia uses for zero-copy video.
class VulkanVideoNode : public QSGSimpleTextureNode {
public:
    VulkanVideoNode() {
        setFiltering(QSGTexture::Linear);
        setOwnsTexture(false); // m_texture managed manually (resize replacement)
    }
    ~VulkanVideoNode() override { destroyImage(); }

    void sync(MpvRenderItem *item);
    // False when there is nothing to display (no video / no first frame yet).
    // Don't use texture() for this: after destroyImage() the base node still
    // holds the stale pointer (setTexture(nullptr) is not allowed).
    bool hasVideoTexture() const { return m_texture != nullptr; }

private:
    void ensureImage(const QSize &size);
    void destroyImage();

    QQuickWindow *m_win = nullptr;
    QSGTexture *m_texture = nullptr;  // wraps m_image
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    QSize m_size;
    bool m_valid = false;             // mpv has rendered at least one frame
    bool m_fboReadyNotified = false;
};

void VulkanVideoNode::sync(MpvRenderItem *item) {
    m_win = item->window();
    MpvController *p = item->player();
    if (!p || !p->handle() || !m_win)
        return;

    if (!p->renderCtx())
        p->ensureRenderCtx(m_win);
    mpv_render_context *ctx = p->renderCtx();
    if (!ctx)
        return;

    if (!p->hasVideo()) {
        // Playback stopped: drop the last frame instead of freezing on it
        // (matches the old path, which stopped drawing on !hasVideo).
        destroyImage();
        return;
    }

    setRect(0, 0, item->width(), item->height());

    const qreal dpr = m_win->devicePixelRatio();
    QSize px(int(item->width() * dpr), int(item->height() * dpr));
    if (px.isEmpty())
        px = QSize(16, 16);

    ensureImage(px);
    if (m_image == VK_NULL_HANDLE)
        return;

    // Must run on every update-callback wakeup — advanced-control mode
    // dispatches mpv core work here — not only when we intend to render.
    const bool newFrame =
        (mpv_render_context_update(ctx) & MPV_RENDER_UPDATE_FRAME) != 0;
    if (!newFrame && m_valid)
        return; // texture still holds the previous frame — nothing to redo

    mpv_vulkan_fbo fbo{};
    fbo.image  = reinterpret_cast<void *>(m_image);
    fbo.w      = px.width();
    fbo.h      = px.height();
    fbo.format = static_cast<int>(VK_FORMAT_R16G16B16A16_SFLOAT);
    fbo.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
               | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT
               | VK_IMAGE_USAGE_SAMPLED_BIT;
    // Pre-seed with GENERAL: a stale mpv-2.dll that predates the out_layout
    // field won't write it back, and GENERAL matches the layout gpu-next's
    // compute-based output passes actually use.
    fbo.out_layout = static_cast<int>(VK_IMAGE_LAYOUT_GENERAL);

    // block=0: without it, mpv_render_context_render() sleeps until the
    // frame's target display time, pinning the render thread — and with it
    // the whole UI — to the video's frame rate.
    int block = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_VULKAN_FBO, &fbo},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    mpv_render_context_render(ctx, params);
    mpv_render_context_report_swap(ctx);
    m_valid = true;

    if (!m_texture) {
        m_texture = QNativeInterface::QSGVulkanTexture::fromNative(
            m_image, static_cast<VkImageLayout>(fbo.out_layout), m_win, m_size);
        setTexture(m_texture);
        if (!m_fboReadyNotified) {
            m_fboReadyNotified = true;
            QMetaObject::invokeMethod(item, "fboReady", Qt::QueuedConnection);
        }
    } else if (QRhiTexture *rt = m_texture->rhiTexture()) {
        rt->setNativeLayout(fbo.out_layout);
    }
    markDirty(QSGNode::DirtyMaterial);
}

// (Re)creates m_image when the node size changes. No-op otherwise.
void VulkanVideoNode::ensureImage(const QSize &size) {
    if (m_image != VK_NULL_HANDLE && m_size == size)
        return;

    QRhi *rhi = m_win->rhi();
    auto *nat = rhi ? static_cast<const QRhiVulkanNativeHandles *>(rhi->nativeHandles()) : nullptr;
    if (!nat || !nat->dev || !nat->physDev || !nat->inst)
        return;
    VkDevice dev = static_cast<VkDevice>(nat->dev);
    QVulkanDeviceFunctions *df = nat->inst->deviceFunctions(dev);
    QVulkanFunctions *f = nat->inst->functions();
    if (!df || !f)
        return;

    destroyImage();

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imgInfo.extent = {static_cast<uint32_t>(size.width()),
                      static_cast<uint32_t>(size.height()), 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                  | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT
                  | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (df->vkCreateImage(dev, &imgInfo, nullptr, &m_image) != VK_SUCCESS) {
        m_image = VK_NULL_HANDLE;
        return;
    }

    VkMemoryRequirements memReq;
    df->vkGetImageMemoryRequirements(dev, m_image, &memReq);

    VkPhysicalDeviceMemoryProperties memProps;
    f->vkGetPhysicalDeviceMemoryProperties(static_cast<VkPhysicalDevice>(nat->physDev), &memProps);
    uint32_t memTypeIdx = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memReq.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memTypeIdx = i;
            break;
        }
    }
    if (memTypeIdx == UINT32_MAX) {
        df->vkDestroyImage(dev, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        return;
    }

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = memTypeIdx;

    if (df->vkAllocateMemory(dev, &allocInfo, nullptr, &m_memory) != VK_SUCCESS) {
        df->vkDestroyImage(dev, m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        return;
    }
    df->vkBindImageMemory(dev, m_image, m_memory, 0);
    m_size = size;
    m_valid = false; // fresh image — needs a real mpv render before display
}

void VulkanVideoNode::destroyImage() {
    if (m_image == VK_NULL_HANDLE && !m_texture)
        return;
    QRhi *rhi = m_win ? m_win->rhi() : nullptr;
    auto *nat = rhi ? static_cast<const QRhiVulkanNativeHandles *>(rhi->nativeHandles()) : nullptr;
    QVulkanDeviceFunctions *df = (nat && nat->dev && nat->inst)
        ? nat->inst->deviceFunctions(static_cast<VkDevice>(nat->dev)) : nullptr;
    if (df && m_image) {
        VkDevice dev = static_cast<VkDevice>(nat->dev);
        // Frames still in flight may be sampling this image (scenegraph
        // invalidation on fullscreen toggles lands here; resize too).
        // Destroying it unguarded is a device loss.
        df->vkDeviceWaitIdle(dev);
        df->vkDestroyImage(dev, m_image, nullptr);
        df->vkFreeMemory(dev, m_memory, nullptr);
    }
    m_image = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
    // The base node may keep the stale texture pointer (setTexture(nullptr)
    // is not allowed); callers never let the node render in that state —
    // either a new texture is set right after (resize) or the node is
    // dropped by updatePaintNode (hasVideoTexture() == false).
    delete m_texture;
    m_texture = nullptr;
    m_valid = false;
}
#endif // QT_CONFIG(vulkan)
} // anonymous namespace

QMutex &VideoRenderNode::renderMutex() { return s_renderMutex; }

QSGRenderNode::StateFlags VideoRenderNode::changedStates() const {
    return ViewportState | ScissorState | RenderTargetState | BlendState;
}

void VideoRenderNode::prepare() {
    MpvController *p = m_item ? m_item->player() : nullptr;
    QQuickWindow *win = m_item ? m_item->window() : nullptr;
    qreal dpr = win ? win->devicePixelRatio() : 1.0;

    // Lock order: s_renderMutex -> s_stateMutex (matches ~MpvController)
    QMutexLocker renderLock(&s_renderMutex);
    QMutexLocker lock(&s_stateMutex);
    if (!p || !p->handle() || !win) {
        s_state.remove(this);
        return;
    }

    auto &st = s_state[this];
    st.item = m_item;
    st.nodeSize = QSize(m_size.width() * dpr, m_size.height() * dpr);
    if (st.nodeSize.isEmpty()) st.nodeSize = QSize(16, 16);

    if (!p->renderCtx())
        p->ensureRenderCtx(win);
    st.renderCtx = p->renderCtx();
    st.hasVideo = p->hasVideo();

    if (!st.fboReady && st.renderCtx) {
        st.fboReady = true;
        QMetaObject::invokeMethod(st.item, "fboReady", Qt::QueuedConnection);
    }
}

void VideoRenderNode::render(const RenderState *state) {
    Q_UNUSED(state);

    // Lock order: s_renderMutex -> s_stateMutex (matches ~MpvController).
    // Acquiring renderMutex first prevents ~MpvController from freeing the
    // render context between our state copy and the actual render call.
    QMutexLocker renderLock(&s_renderMutex);

    VideoRenderState st;
    {
        QMutexLocker lock(&s_stateMutex);
        auto it = s_state.find(this);
        if (it == s_state.end())
            return;
        st = *it;
    }

    if (!st.renderCtx || !st.hasVideo) return;

    auto *win = st.item ? st.item->window() : nullptr;
    if (!win) return;

    QRhi *rhi = win->rhi();
    if (!rhi) {
        return;
    }

    // Qt Quick redraws the entire scene into a cleared backbuffer on every
    // frame it composes, so once video exists this node must put pixels on
    // the render target on EVERY call — any early "skip" here presents the
    // window clear color in the video area for one frame, visible as a black
    // flash (mpv pings ~1/sec even paused -> 1 Hz flicker; a control-bar
    // fade animation composes ~12 frames -> a burst of flashes).
    //
    // The only work that MAY be skipped is the expensive mpv render into the
    // cached offscreen target. The update callback fires for reasons
    // unrelated to new frames — render.h: "the callback can now be called
    // even if there is no new frame. The API user should call
    // mpv_render_context_update() and interpret the return value for whether
    // a new frame should be rendered." So ask mpv; with no new frame, the
    // cached offscreen content is re-blitted instead (OpenGL), or mpv
    // redraws the previous frame itself (D3D11, which has no cache).
    const bool newFrame =
        (mpv_render_context_update(st.renderCtx) & MPV_RENDER_UPDATE_FRAME) != 0;

#ifdef Q_OS_WIN
    // ── D3D11 render API backend ──
    // No offscreen cache here — mpv draws straight into Qt's render target,
    // which Qt cleared this frame. Render unconditionally: with no new frame
    // mpv just redraws the previous one (documented mpv_render_context_render
    // behavior); skipping would present the clear color for a frame.
    if (rhi->backend() == QRhi::D3D11) {
        win->beginExternalCommands();

        auto *nat = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
        if (!nat || !nat->context) {
            win->endExternalCommands();
            return;
        }
        ID3D11DeviceContext *ctx =
            static_cast<ID3D11DeviceContext *>(nat->context);

        // Retrieve the current render target view from the output merger
        ID3D11RenderTargetView *rtv = nullptr;
        ctx->OMGetRenderTargets(1, &rtv, nullptr);
        if (!rtv) {
            win->endExternalCommands();
            return;
        }

        ID3D11Resource *res = nullptr;
        rtv->GetResource(&res);
        ID3D11Texture2D *tex = static_cast<ID3D11Texture2D *>(res);

        mpv_d3d11_fbo fbo{tex, st.nodeSize.width(), st.nodeSize.height()};
        int block = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_D3D11_FBO, &fbo},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        mpv_render_context_render(st.renderCtx, params);
        mpv_render_context_report_swap(st.renderCtx);

        rtv->Release();
        res->Release();

        win->endExternalCommands();
        return;
    }
#endif // Q_OS_WIN

    // ── OpenGL render API backend (fallback) ──
    //
    // gpu-next ignores MPV_RENDER_PARAM_FLIP_Y (documented as unsupported),
    // so video comes out upside-down when rendering directly to Qt's FBO.
    // Workaround: render mpv to an offscreen FBO, then glBlitFramebuffer
    // with swapped Y to the draw FBO. FBO+texture cached across frames — only
    // recreated when node size changes (e.g. resize / HDR toggle).
    win->beginExternalCommands();

    auto *glCtx = QOpenGLContext::currentContext();
    if (!glCtx) {
        win->endExternalCommands();
        return;
    }
    auto *f = glCtx->extraFunctions();

    GLint drawFbo = 0;
    f->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    const int w = st.nodeSize.width();
    const int h = st.nodeSize.height();

    // Cache offscreen FBO — only recreate when size changes
    if (st.offFboSize != st.nodeSize) {
        if (st.offFbo)  f->glDeleteFramebuffers(1, &st.offFbo);
        if (st.offTex)  f->glDeleteTextures(1, &st.offTex);
        f->glGenFramebuffers(1, &st.offFbo);
        f->glGenTextures(1, &st.offTex);
        f->glBindTexture(GL_TEXTURE_2D, st.offTex);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, st.offTex, 0);
        st.offValid = false;  // fresh texture — force an mpv render below
        // Write back to master state so next frame sees the cached FBO
        { QMutexLocker l(&s_stateMutex);
          auto &ms = s_state[this];
          ms.offFbo = st.offFbo;
          ms.offTex = st.offTex;
          ms.offFboSize = st.nodeSize;
          ms.offValid = false; }
    }

    if (newFrame || !st.offValid) {
        f->glBindFramebuffer(GL_FRAMEBUFFER, st.offFbo);

        mpv_opengl_fbo mpvFbo{
            static_cast<int>(st.offFbo), w, h, 0
        };
        int block = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        mpv_render_context_render(st.renderCtx, params);
        mpv_render_context_report_swap(st.renderCtx);

        QMutexLocker l(&s_stateMutex);
        s_state[this].offValid = true;
    }

    // Blit with Y-flip — on every composed frame, even when mpv had nothing
    // new: Qt cleared this backbuffer, skipping the blit shows the clear color.
    f->glBindFramebuffer(GL_READ_FRAMEBUFFER, st.offFbo);
    f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
    f->glBlitFramebuffer(0, 0, w, h,
                         0, h, w, 0,
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);

    win->endExternalCommands();
}

void VideoRenderNode::releaseResources() {
    {
        QMutexLocker lock(&s_stateMutex);
        auto it = s_state.find(this);
        if (it != s_state.end()) {
            if (it->offFbo || it->offTex) {
                if (auto *ctx = QOpenGLContext::currentContext()) {
                    auto *f = ctx->functions();
                    if (it->offFbo) f->glDeleteFramebuffers(1, &it->offFbo);
                    if (it->offTex) f->glDeleteTextures(1, &it->offTex);
                }
            }
        }
        s_state.remove(this);
    }
}

void VideoRenderNode::detachController(MpvController *controller) {
    QMutexLocker lock(&s_stateMutex);
    for (auto it = s_state.begin(); it != s_state.end(); ) {
        if (it->item && it->item->player() == controller) {
            it->renderCtx = nullptr;
            it = s_state.erase(it);
        } else {
            ++it;
        }
    }
}

MpvRenderItem::MpvRenderItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
    // Rendering is driven by mpv's update callback (renderUpdateNeeded),
    // not a timer — avoids busy-waiting and double-driving the render loop.
}

MpvRenderItem::~MpvRenderItem() = default;

void MpvRenderItem::geometryChange(const QRectF &newGeo, const QRectF &oldGeo) {
    QQuickItem::geometryChange(newGeo, oldGeo);
    if (newGeo.size() != oldGeo.size())
        update();
}

void MpvRenderItem::itemChange(ItemChange change, const ItemChangeData &data) {
    if (change == ItemSceneChange) {
        if (data.window)
            data.window->installEventFilter(this);
    }
    QQuickItem::itemChange(change, data);
}

bool MpvRenderItem::eventFilter(QObject *obj, QEvent *event) {
    if (event->type() == QEvent::MouseMove)
        emit mouseMoved();
    return QQuickItem::eventFilter(obj, event);
}

void MpvRenderItem::setPlayer(MpvController *p) {
    if (m_player != p) {
        if (m_player) {
            disconnect(m_player, &MpvController::hasVideoChanged,
                       this, &QQuickItem::update);
            disconnect(m_renderUpdateConn);
            disconnect(m_player, &QObject::destroyed, this, nullptr);
        }
        m_player = p;
        if (m_player) {
            connect(m_player, &MpvController::hasVideoChanged,
                    this, &QQuickItem::update);
            m_renderUpdateConn = connect(m_player, &MpvController::renderUpdateNeeded,
                    this, [this]() { m_dirty = true; update(); });
            connect(m_player, &QObject::destroyed, this, [this]() {
                m_player = nullptr;
                emit playerChanged();
                update();
            });
        }
        emit playerChanged();
        update();
    }
}

QSGNode *MpvRenderItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
#if QT_CONFIG(vulkan)
    if (QQuickWindow *win = window(); win && win->rhi()
            && win->rhi()->backend() == QRhi::Vulkan) {
        auto *node = static_cast<VulkanVideoNode *>(old);
        if (!node)
            node = new VulkanVideoNode;
        m_dirty = false; // node->sync asks mpv itself whether a frame is due
        node->sync(this);
        if (!node->hasVideoTexture()) {
            // Nothing to show (no video, or first frame not rendered yet).
            delete node;
            return nullptr;
        }
        return node;
    }
#endif
    VideoRenderNode *node = static_cast<VideoRenderNode *>(old);
    if (!node) {
        node = new VideoRenderNode;
        node->m_item = this;
    }
    node->m_size = QSizeF(width(), height());
    if (m_dirty) {
        node->markDirty(QSGNode::DirtyMaterial);
        m_dirty = false;
    }
    return node;
}
