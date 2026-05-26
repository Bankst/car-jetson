#include "AAVideoItem.h"
#include "AAVideoDecoder.h"
#include "AASessionController.h"
#include "QmlInputDevice.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QtQuick/QSGTextureProvider>

AAVideoItem::AAVideoItem(QQuickItem* parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptTouchEvents(true);
}

AAVideoItem::~AAVideoItem() {
    if (m_textureId) {
        auto* f = QOpenGLContext::currentContext()
                      ? QOpenGLContext::currentContext()->functions()
                      : nullptr;
        if (f) f->glDeleteTextures(1, &m_textureId);
    }
}

void AAVideoItem::setSession(AASessionController* ctrl) {
    if (m_session == ctrl) return;
    m_session = ctrl;
    emit sessionChanged();
    if (ctrl) {
        setDecoder(ctrl->decoder());
        auto input = ctrl->inputDevice();
        connect(this, &AAVideoItem::touchDown, this,
                [input](float x, float y){ input->injectTouchDown(x, y); });
        connect(this, &AAVideoItem::touchMove, this,
                [input](float x, float y){ input->injectTouchMove(x, y); });
        connect(this, &AAVideoItem::touchUp, this,
                [input](float x, float y){ input->injectTouchUp(x, y); });
    }
}

void AAVideoItem::setDecoder(std::shared_ptr<AAVideoDecoder> decoder) {
    m_decoder = std::move(decoder);
    if (m_decoder) {
        m_decoder->setFrameReadyCallback([this]{
            QMetaObject::invokeMethod(this, &AAVideoItem::onFrameReady,
                                      Qt::QueuedConnection);
        });
    }
}

void AAVideoItem::onFrameReady() {
    if (!m_hasFrame) {
        m_hasFrame = true;
        emit hasFrameChanged();
    }
    update();
}

QSGNode* AAVideoItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    if (!m_decoder) {
        delete old;
        return nullptr;
    }

    auto frame = m_decoder->latestFrame();
    if (!frame || frame->rgba.empty()) {
        delete old;
        return nullptr;
    }

    auto* f = QOpenGLContext::currentContext()->functions();

    if (!m_textureId) {
        f->glGenTextures(1, &m_textureId);
        f->glBindTexture(GL_TEXTURE_2D, m_textureId);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        f->glBindTexture(GL_TEXTURE_2D, m_textureId);
    }

    if (frame->width != m_lastW || frame->height != m_lastH) {
        f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame->width, frame->height,
                        0, GL_RGBA, GL_UNSIGNED_BYTE, frame->rgba.data());
        m_lastW = frame->width;
        m_lastH = frame->height;
    } else {
        f->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame->width, frame->height,
                           GL_RGBA, GL_UNSIGNED_BYTE, frame->rgba.data());
    }

    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    if (!node) {
        node = new QSGSimpleTextureNode();
    }

    QSGTexture* tex = QNativeInterface::QSGOpenGLTexture::fromNative(
        m_textureId, window(), QSize(frame->width, frame->height));
    node->setTexture(tex);
    node->setOwnsTexture(true);
    node->setRect(boundingRect());
    node->setTextureCoordinatesTransform(QSGSimpleTextureNode::NoTransform);
    node->markDirty(QSGNode::DirtyMaterial);

    return node;
}

void AAVideoItem::mousePressEvent(QMouseEvent* ev) {
    float nx = static_cast<float>(ev->position().x()) / width();
    float ny = static_cast<float>(ev->position().y()) / height();
    emit touchDown(nx, ny);
    ev->accept();
}

void AAVideoItem::mouseMoveEvent(QMouseEvent* ev) {
    float nx = static_cast<float>(ev->position().x()) / width();
    float ny = static_cast<float>(ev->position().y()) / height();
    emit touchMove(nx, ny);
    ev->accept();
}

void AAVideoItem::mouseReleaseEvent(QMouseEvent* ev) {
    float nx = static_cast<float>(ev->position().x()) / width();
    float ny = static_cast<float>(ev->position().y()) / height();
    emit touchUp(nx, ny);
    ev->accept();
}
