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
// Fallback: mpv_vulkan_fbo struct (from render_vulkan.h)
#define MPV_RENDER_PARAM_VULKAN_FBO ((mpv_render_param_type)25)
struct mpv_vulkan_fbo { void *image; int format, usage; int w, h; };
#endif
}

#include <QOpenGLContext>
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
#if __has_include(<private/qrhivulkan_p.h>)
#include <private/qrhivulkan_p.h>
#define MFPLAYER_HAS_VK_SWAPCHAIN_PRIVATE 1
#endif
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
    QSize nodeSize;
    MpvRenderItem *item = nullptr;
};

QHash<VideoRenderNode *, VideoRenderState> s_state;
QMutex s_stateMutex;
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

#if QT_CONFIG(vulkan)
    // ── Vulkan render API backend ──
    if (rhi->backend() == QRhi::Vulkan) {
        win->beginExternalCommands();

        QRhiSwapChain *sc = win->swapChain();
        if (!sc) { win->endExternalCommands(); return; }

        VkImage vkImg = VK_NULL_HANDLE;
        QSize rtSize = st.nodeSize;
        VkFormat vkFmt = VK_FORMAT_B8G8R8A8_UNORM;

#ifdef MFPLAYER_HAS_VK_SWAPCHAIN_PRIVATE
        auto *vkSc = static_cast<QVkSwapChain *>(sc);
        quint32 idx = vkSc->currentImageIndex;
        vkImg = vkSc->imageRes[idx].image;
        rtSize = sc->currentPixelSize();
        QVariant hdr = win->property("_hdrActive");
        vkFmt = (hdr.toBool())
            ? VK_FORMAT_A2B10G10R10_UNORM_PACK32
            : VK_FORMAT_B8G8R8A8_UNORM;
#else
        QRhiRenderTarget *rt = sc->currentFrameRenderTarget();
        if (rt && rt->resourceType() == QRhiResource::TextureRenderTarget) {
            auto *trt = static_cast<QRhiTextureRenderTarget *>(rt);
            const auto desc = trt->description();
            auto it = desc.cbeginColorAttachments();
            if (it != desc.cendColorAttachments() && it->texture()) {
                auto nt = it->texture()->nativeTexture();
                vkImg = reinterpret_cast<VkImage>(nt.object);
                rtSize = it->texture()->pixelSize();
            }
        }
#endif

        if (!vkImg || vkImg == VK_NULL_HANDLE) {
            win->endExternalCommands();
            return;
        }

        // ── Simple direct render (same shape as D3D11 path) ──────
        // mpv/libplacebo handles all VkCommandBuffer work and layout
        // transitions internally.  We just provide the target VkImage.
        mpv_render_context_update(st.renderCtx);

        mpv_vulkan_fbo fbo{};
        fbo.image  = reinterpret_cast<void*>(vkImg);
        fbo.w      = rtSize.width();
        fbo.h      = rtSize.height();
        fbo.format = static_cast<int>(vkFmt);
        fbo.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        int advanced = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_VULKAN_FBO, &fbo},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        mpv_render_context_render(st.renderCtx, params);
        mpv_render_context_report_swap(st.renderCtx);

        win->endExternalCommands();
        return;
    }
#endif // QT_CONFIG(vulkan)

#ifdef Q_OS_WIN
    // ── D3D11 render API backend ──
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
        int advanced = 1;
        int block = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_D3D11_FBO, &fbo},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
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
    // Workaround: render mpv to a temporary offscreen FBO, then glBlitFramebuffer
    // with swapped Y to the draw FBO — a pure-GPU copy, effectively zero cost.
    {
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

        // Create temporary offscreen FBO + texture for mpv
        GLuint offFbo = 0, offTex = 0;
        f->glGenFramebuffers(1, &offFbo);
        f->glGenTextures(1, &offTex);
        f->glBindTexture(GL_TEXTURE_2D, offTex);
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glBindFramebuffer(GL_FRAMEBUFFER, offFbo);
        f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_TEXTURE_2D, offTex, 0);

        mpv_render_context_update(st.renderCtx);

        mpv_opengl_fbo mpvFbo{
            static_cast<int>(offFbo), w, h, 0
        };
        int advanced = 1;
        int block = 0;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
            {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        mpv_render_context_render(st.renderCtx, params);
        mpv_render_context_report_swap(st.renderCtx);

        // Blit with Y-flip:
        // src rect (0,0)-(w,h) in offscreen FBO has origin at bottom-left
        // dst rect (0,h)-(w,0) in draw FBO inverts Y → top-left origin
        f->glBindFramebuffer(GL_READ_FRAMEBUFFER, offFbo);
        f->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        f->glBlitFramebuffer(0, 0, w, h,
                             0, h, w, 0,
                             GL_COLOR_BUFFER_BIT, GL_NEAREST);

        f->glDeleteTextures(1, &offTex);
        f->glDeleteFramebuffers(1, &offFbo);

        win->endExternalCommands();
    }
}

void VideoRenderNode::releaseResources() {
    QMutexLocker lock(&s_stateMutex);
    s_state.remove(this);
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
