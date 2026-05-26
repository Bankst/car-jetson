#pragma once

#include <QQuickItem>
#include <QSGNode>
#include <memory>

class AAVideoDecoder;

class AASessionController;

class AAVideoItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    Q_PROPERTY(AASessionController* session READ session WRITE setSession NOTIFY sessionChanged)

public:
    explicit AAVideoItem(QQuickItem* parent = nullptr);
    ~AAVideoItem() override;

    bool hasFrame() const { return m_hasFrame; }

    AASessionController* session() const { return m_session; }
    void setSession(AASessionController* ctrl);

    void setDecoder(std::shared_ptr<AAVideoDecoder> decoder);

signals:
    void hasFrameChanged();
    void sessionChanged();
    void touchDown(float normX, float normY);
    void touchMove(float normX, float normY);
    void touchUp(float normX, float normY);

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseReleaseEvent(QMouseEvent* ev) override;

private slots:
    void onFrameReady();

private:
    AASessionController* m_session = nullptr;
    std::shared_ptr<AAVideoDecoder> m_decoder;
    uint32_t m_textureId = 0;
    bool m_hasFrame = false;
    int m_lastW = 0;
    int m_lastH = 0;
};
