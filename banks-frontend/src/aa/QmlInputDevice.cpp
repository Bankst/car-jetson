#include "QmlInputDevice.h"

using namespace f1x::openauto::autoapp::projection;
using PointerAction = aap_protobuf::service::inputsource::message::PointerAction;

namespace aa {

QmlInputDevice::QmlInputDevice(int displayW, int displayH)
    : m_displayW(displayW), m_displayH(displayH) {}

void QmlInputDevice::start(IInputDeviceEventHandler& handler) {
    std::lock_guard lk(m_mtx);
    m_handler = &handler;
}

void QmlInputDevice::stop() {
    std::lock_guard lk(m_mtx);
    m_handler = nullptr;
}

QmlInputDevice::ButtonCodes QmlInputDevice::getSupportedButtonCodes() const {
    using KC = aap_protobuf::service::media::sink::message::KeyCode;
    return {
        KC::KEYCODE_HOME,
        KC::KEYCODE_BACK,
        KC::KEYCODE_DPAD_UP,
        KC::KEYCODE_DPAD_DOWN,
        KC::KEYCODE_DPAD_LEFT,
        KC::KEYCODE_DPAD_RIGHT,
        KC::KEYCODE_DPAD_CENTER,
        KC::KEYCODE_SEARCH,
    };
}

void QmlInputDevice::setDisplaySize(int w, int h) {
    std::lock_guard lk(m_mtx);
    m_displayW = w;
    m_displayH = h;
}

bool QmlInputDevice::hasTouchscreen() const { return true; }

QRect QmlInputDevice::getTouchscreenGeometry() const {
    return QRect(0, 0, m_displayW, m_displayH);
}

void QmlInputDevice::injectTouchDown(float normX, float normY) {
    std::lock_guard lk(m_mtx);
    if (!m_handler) return;
    TouchEvent ev;
    ev.type = PointerAction::ACTION_DOWN;
    ev.actionIndex = 0;
    ev.pointers.push_back({
        static_cast<uint32_t>(normX * m_displayW),
        static_cast<uint32_t>(normY * m_displayH),
        0
    });
    m_handler->onTouchEvent(ev);
}

void QmlInputDevice::injectTouchMove(float normX, float normY) {
    std::lock_guard lk(m_mtx);
    if (!m_handler) return;
    TouchEvent ev;
    ev.type = PointerAction::ACTION_MOVED;
    ev.actionIndex = 0;
    ev.pointers.push_back({
        static_cast<uint32_t>(normX * m_displayW),
        static_cast<uint32_t>(normY * m_displayH),
        0
    });
    m_handler->onTouchEvent(ev);
}

void QmlInputDevice::injectTouchUp(float normX, float normY) {
    std::lock_guard lk(m_mtx);
    if (!m_handler) return;
    TouchEvent ev;
    ev.type = PointerAction::ACTION_UP;
    ev.actionIndex = 0;
    ev.pointers.push_back({
        static_cast<uint32_t>(normX * m_displayW),
        static_cast<uint32_t>(normY * m_displayH),
        0
    });
    m_handler->onTouchEvent(ev);
}

}
