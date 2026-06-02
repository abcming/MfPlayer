#include "platform/rendering/mpv/mpvrenderitem.h"
#include "platform/rendering/mpv/mpvcontroller.h"

extern "C" {
#include <mpv/render_gl.h>
#ifdef Q_OS_WIN
#include <mpv/render_d3d11.h>
#endif
}

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QMutex>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#include <d3d11.h>
// NOTE: qrhi_platform.h is a Qt private header. QRhiD3D11NativeHandles has been
// stable since Qt 6.2, but may change across major Qt versions. If upgrading
// past Qt 6.x, verify this struct still exists.
#include <rhi/qrhi_platform.h>
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

#ifdef Q_OS_WIN
    // D3D11 render API backend
    win->beginExternalCommands();

    QRhi *rhi = win->rhi();
    if (!rhi) {
        win->endExternalCommands();
        return;
    }
    auto *native = static_cast<const QRhiD3D11NativeHandles *>(
        rhi->nativeHandles());
    if (!native || !native->context) {
        win->endExternalCommands();
        return;
    }
    ID3D11DeviceContext *ctx =
        static_cast<ID3D11DeviceContext *>(native->context);

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
#else
    // OpenGL render API backend
    win->beginExternalCommands();

    auto *glCtx = QOpenGLContext::currentContext();
    if (!glCtx) {
        win->endExternalCommands();
        return;
    }
    auto *f = glCtx->functions();
    GLint drawFbo = 0;
    f->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    f->glViewport(0, 0, st.nodeSize.width(), st.nodeSize.height());
    f->glDisable(GL_SCISSOR_TEST);

    mpv_opengl_fbo mpvFbo{
        drawFbo,
        st.nodeSize.width(),
        st.nodeSize.height(),
        0
    };
    int flip_y = 1;
    int advanced = 1;
    int block = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpvFbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    mpv_render_context_render(st.renderCtx, params);
    mpv_render_context_report_swap(st.renderCtx);

    win->endExternalCommands();
#endif
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
