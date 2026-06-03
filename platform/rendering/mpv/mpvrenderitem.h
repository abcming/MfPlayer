#pragma once
#include <QQuickItem>
#include <QSGRenderNode>
#include <QPointer>
#include <QMutex>

class MpvController;
class MpvRenderItem;

class VideoRenderNode : public QSGRenderNode {
public:
    QRectF rect() const override { return QRectF(QPointF(), m_size); }
    StateFlags changedStates() const override;
    RenderingFlags flags() const override { return BoundedRectRendering; }

    void prepare() override;
    void render(const RenderState *state) override;
    void releaseResources() override;

    // Called by ~MpvController to invalidate all render state for a given
    // controller before it destroys the mpv render context.
    static void detachController(MpvController *controller);
    // Returns the mutex that serializes render() and ~MpvController.
    static QMutex &renderMutex();

private:
    friend class MpvRenderItem;
    QSizeF m_size;
    QPointer<MpvRenderItem> m_item;
};

class MpvRenderItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(MpvController *player READ player WRITE setPlayer NOTIFY playerChanged FINAL)

public:
    explicit MpvRenderItem(QQuickItem *parent = nullptr);
    ~MpvRenderItem() override;

    MpvController *player() const { return m_player; }
    void setPlayer(MpvController *p);

    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

signals:
    void playerChanged();
    void fboReady();
    void mouseMoved();

protected:
    void geometryChange(const QRectF &newGeo, const QRectF &oldGeo) override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QPointer<MpvController> m_player;
    QMetaObject::Connection m_renderUpdateConn;
    bool m_dirty = false;  // set on renderUpdateNeeded; cleared in updatePaintNode
};
