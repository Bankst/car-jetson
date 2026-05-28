#pragma once

#include <f1x/openauto/autoapp/Projection/IInputDevice.hpp>
#include <f1x/openauto/autoapp/Projection/IInputDeviceEventHandler.hpp>
#include <mutex>

namespace aa {

class QmlInputDevice : public f1x::openauto::autoapp::projection::IInputDevice {
public:
    QmlInputDevice(int displayW, int displayH);

    void start(f1x::openauto::autoapp::projection::IInputDeviceEventHandler& handler) override;
    void stop() override;
    ButtonCodes getSupportedButtonCodes() const override;
    bool hasTouchscreen() const override;
    QRect getTouchscreenGeometry() const override;

    void setDisplaySize(int w, int h);
    void injectTouchDown(float normX, float normY);
    void injectTouchMove(float normX, float normY);
    void injectTouchUp(float normX, float normY);

private:
    int m_displayW;
    int m_displayH;
    std::mutex m_mtx;
    f1x::openauto::autoapp::projection::IInputDeviceEventHandler* m_handler = nullptr;
};

}
