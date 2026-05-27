#pragma once

#include <QObject>
#include <QTimer>
#include <boost/asio/io_service.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

class AAVideoDecoder;

// Full include needed — Q_PROPERTY(BluetoothPairingAgent*) requires a
// complete type for Qt6 MOC metatype registration.
#include "BluetoothPairingAgent.h"

namespace aa { class QmlInputDevice; class PipeWireAudioInput; }

namespace aasdk::usb { class USBWrapper; class IUSBHub; class IConnectedAccessoriesEnumerator; }
namespace aasdk::tcp { class ITCPWrapper; }
namespace f1x::openauto::autoapp { class App; }
namespace f1x::openauto::autoapp::service { class IAndroidAutoEntityFactory; }
namespace f1x::openauto::btservice { class BluetoothHandler; }

class AASessionController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool sessionActive READ sessionActive NOTIFY sessionActiveChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(float micLevel READ micLevel NOTIFY micLevelChanged)
    Q_PROPERTY(float musicLevel READ musicLevel NOTIFY audioLevelsChanged)
    Q_PROPERTY(float guidanceLevel READ guidanceLevel NOTIFY audioLevelsChanged)
    Q_PROPERTY(float systemLevel READ systemLevel NOTIFY audioLevelsChanged)
    Q_PROPERTY(BluetoothPairingAgent* pairingAgent READ pairingAgent CONSTANT)
    Q_PROPERTY(int videoResolution READ videoResolution WRITE setVideoResolution NOTIFY videoResolutionChanged)
    Q_PROPERTY(int videoFps READ videoFps WRITE setVideoFps NOTIFY videoFpsChanged)
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(bool wirelessActive READ wirelessActive NOTIFY wirelessActiveChanged)

public:
    explicit AASessionController(QObject* parent = nullptr);
    ~AASessionController() override;

    bool connected() const { return m_connected; }
    bool sessionActive() const { return m_sessionActive; }
    QString status() const { return m_status; }
    float micLevel() const;
    float musicLevel() const;
    float guidanceLevel() const;
    float systemLevel() const;

    int videoResolution() const { return m_videoResolution; }
    void setVideoResolution(int v);
    int videoFps() const { return m_videoFps; }
    void setVideoFps(int v);
    bool autoStart() const { return m_autoStart; }
    void setAutoStart(bool v);
    bool wirelessActive() const { return m_btHandler != nullptr; }

    Q_INVOKABLE void activate();
    Q_INVOKABLE void deactivate();
    Q_INVOKABLE void suspend();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void startWireless();
    Q_INVOKABLE void stopWireless();

    std::shared_ptr<AAVideoDecoder> decoder() const { return m_decoder; }
    std::shared_ptr<aa::QmlInputDevice> inputDevice() const { return m_inputDevice; }
    BluetoothPairingAgent* pairingAgent() const { return m_pairingAgent; }

signals:
    void connectedChanged();
    void sessionActiveChanged();
    void statusChanged();
    void micLevelChanged();
    void audioLevelsChanged();
    void videoResolutionChanged();
    void videoFpsChanged();
    void autoStartChanged();
    void wirelessActiveChanged();

private:
    void startUSB();
    void stopUSB();
    void startBtHandler();
    void setStatus(const QString& s);
    void setConnected(bool c);
    void onFirstFrame();
    void onPhoneDisconnected();
    void onHeartbeatTimeout();

    bool m_connected = false;
    bool m_sessionActive = false;
    QString m_status = QStringLiteral("Idle");
    std::atomic<bool> m_firstFrameSeen{false};

    // Frame heartbeat: timestamp of last decoded frame (written from decoder
    // thread, read from QTimer on GUI thread — atomic for thread safety)
    std::atomic<int64_t> m_lastFrameEpochMs{0};
    QTimer m_heartbeatTimer;
    QTimer m_micLevelTimer;
    float m_lastMicLevel = 0.0f;
    static constexpr int kHeartbeatIntervalMs = 2000;
    static constexpr int kHeartbeatTimeoutMs = 4000;

    std::shared_ptr<AAVideoDecoder> m_decoder;
    std::shared_ptr<aa::QmlInputDevice> m_inputDevice;
    std::shared_ptr<aa::PipeWireAudioInput> m_micInput;

    std::unique_ptr<boost::asio::io_service> m_ioService;
    std::unique_ptr<aasdk::usb::USBWrapper> m_usbWrapper;
    std::unique_ptr<aasdk::tcp::ITCPWrapper> m_tcpWrapper;
    std::shared_ptr<void> m_queryFactory;
    std::shared_ptr<void> m_chainFactory;
    std::shared_ptr<void> m_serviceFactory;
    std::shared_ptr<aasdk::usb::IUSBHub> m_usbHub;
    std::shared_ptr<aasdk::usb::IConnectedAccessoriesEnumerator> m_accessoryEnum;
    std::unique_ptr<f1x::openauto::autoapp::service::IAndroidAutoEntityFactory> m_entityFactory;
    std::shared_ptr<f1x::openauto::autoapp::App> m_app;
    std::unique_ptr<f1x::openauto::btservice::BluetoothHandler> m_btHandler;
    std::shared_ptr<void> m_aaConfig;
    BluetoothPairingAgent* m_pairingAgent = nullptr;

    std::vector<std::thread> m_ioThreads;
    std::vector<std::thread> m_usbThreads;

    // 3 = VIDEO_1920x1080, 1 = VIDEO_FPS_60 (protobuf enum values)
    int m_videoResolution = 3;
    int m_videoFps = 1;
    bool m_autoStart = true;
};
