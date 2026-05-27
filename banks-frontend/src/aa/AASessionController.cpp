#include "AASessionController.h"
#include "AAVideoDecoder.h"
#include "AAServiceFactory.h"
#include "AAConfiguration.h"
#include "QmlInputDevice.h"
#include "PipeWireAudioInput.h"
#include "PipeWireAudioOutput.h"
#include "BluetoothPairingAgent.h"

#include <boost/asio.hpp>
#include <libusb-1.0/libusb.h>

#include <aasdk/USB/USBWrapper.hpp>
#include <aasdk/USB/USBHub.hpp>
#include <aasdk/USB/ConnectedAccessoriesEnumerator.hpp>
#include <aasdk/USB/AccessoryModeQueryChainFactory.hpp>
#include <aasdk/USB/AccessoryModeQueryFactory.hpp>
#include <aasdk/TCP/TCPWrapper.hpp>

#include <f1x/openauto/autoapp/App.hpp>
#include <f1x/openauto/autoapp/Service/AndroidAutoEntityFactory.hpp>
#include <f1x/openauto/btservice/BluetoothHandler.hpp>
#include <f1x/openauto/btservice/AndroidBluetoothService.hpp>
#include <f1x/openauto/Common/Log.hpp>

#include <QCoreApplication>
#include <QDebug>

// ---------------------------------------------------------------------------
// ObservableApp — thin subclass of App that exposes the quit callback
// to AASessionController without modifying the openauto library.
// ---------------------------------------------------------------------------
namespace {

class ObservableApp : public f1x::openauto::autoapp::App {
public:
    using QuitCallback = std::function<void()>;

    ObservableApp(boost::asio::io_service& ioService,
                  aasdk::usb::USBWrapper& usbWrapper,
                  aasdk::tcp::ITCPWrapper& tcpWrapper,
                  f1x::openauto::autoapp::service::IAndroidAutoEntityFactory& factory,
                  aasdk::usb::IUSBHub::Pointer usbHub,
                  aasdk::usb::IConnectedAccessoriesEnumerator::Pointer enumerator)
        : App(ioService, usbWrapper, tcpWrapper, factory,
              std::move(usbHub), std::move(enumerator))
    {}

    void setQuitCallback(QuitCallback cb) { m_quitCb = std::move(cb); }

    void onAndroidAutoQuit() override {
        OPENAUTO_LOG(info) << "[ObservableApp] onAndroidAutoQuit — notifying controller";
        if (m_quitCb) m_quitCb();
        App::onAndroidAutoQuit();   // preserves reconnect-wait logic
    }

private:
    QuitCallback m_quitCb;
};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// AASessionController
// ---------------------------------------------------------------------------

AASessionController::AASessionController(QObject* parent)
    : QObject(parent)
    , m_decoder(std::make_shared<AAVideoDecoder>())
    , m_inputDevice(std::make_shared<aa::QmlInputDevice>(1920, 1080))
    , m_micInput(std::make_shared<aa::PipeWireAudioInput>())
{
    m_heartbeatTimer.setInterval(kHeartbeatIntervalMs);
    connect(&m_heartbeatTimer, &QTimer::timeout, this, &AASessionController::onHeartbeatTimeout);

    m_micLevelTimer.setInterval(50);
    connect(&m_micLevelTimer, &QTimer::timeout, this, [this]{
        float lvl = m_micInput ? m_micInput->peakLevel() : 0.0f;
        if (lvl != m_lastMicLevel) {
            m_lastMicLevel = lvl;
            emit micLevelChanged();
        }
        emit audioLevelsChanged();
    });

    // BlueZ pairing agent — deferred to avoid blocking constructor if
    // D-Bus call hangs (e.g. GNOME BT agent holds the default slot).
    m_pairingAgent = new BluetoothPairingAgent(this);
    QTimer::singleShot(500, this, [this]{
        if (!m_pairingAgent->registerAgent()) {
            OPENAUTO_LOG(warning) << "[AASessionController] BT pairing agent registration failed";
        }
    });
}

float AASessionController::micLevel() const {
    return m_micInput ? m_micInput->peakLevel() : 0.0f;
}

float AASessionController::musicLevel() const {
    auto sf = std::static_pointer_cast<aa::AAServiceFactory>(m_serviceFactory);
    return (sf && sf->mediaAudio()) ? sf->mediaAudio()->peakLevel() : 0.0f;
}

float AASessionController::guidanceLevel() const {
    auto sf = std::static_pointer_cast<aa::AAServiceFactory>(m_serviceFactory);
    return (sf && sf->guidanceAudio()) ? sf->guidanceAudio()->peakLevel() : 0.0f;
}

float AASessionController::systemLevel() const {
    auto sf = std::static_pointer_cast<aa::AAServiceFactory>(m_serviceFactory);
    return (sf && sf->systemAudio()) ? sf->systemAudio()->peakLevel() : 0.0f;
}

AASessionController::~AASessionController() {
    stopUSB();
}

void AASessionController::activate() {
    OPENAUTO_LOG(info) << "[AASessionController] activate() called, sessionActive=" << m_sessionActive;
    if (m_sessionActive) return;
    OPENAUTO_LOG(info) << "[AASessionController] starting AA session";
    m_sessionActive = true;
    emit sessionActiveChanged();
    m_micLevelTimer.start();
    startUSB();
}

void AASessionController::deactivate() {
    OPENAUTO_LOG(info) << "[AASessionController] deactivate() called, sessionActive=" << m_sessionActive;
    if (!m_sessionActive) return;
    m_micLevelTimer.stop();
    stopUSB();
    m_sessionActive = false;
    emit sessionActiveChanged();
    setConnected(false);
    setStatus(QStringLiteral("Idle"));
}

void AASessionController::setStatus(const QString& s) {
    if (m_status != s) {
        m_status = s;
        OPENAUTO_LOG(info) << "[AASessionController] status -> " << s.toStdString();
        emit statusChanged();
    }
}

void AASessionController::setConnected(bool c) {
    if (m_connected != c) {
        m_connected = c;
        OPENAUTO_LOG(info) << "[AASessionController] connected -> " << c;
        emit connectedChanged();
    }
}

// Called (via QueuedConnection) on the GUI thread when the decoder emits
// its very first frame.
void AASessionController::onFirstFrame() {
    OPENAUTO_LOG(info) << "[AASessionController] first video frame received";
    setConnected(true);
    setStatus(QStringLiteral("Android Auto active"));

    // Seed the heartbeat clock and start the timer
    m_lastFrameEpochMs.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
        std::memory_order_relaxed);
    m_heartbeatTimer.start();
}

// Called from the ObservableApp quit callback (runs on an io_service thread).
// Marshalled to the GUI thread via QueuedConnection.
void AASessionController::onPhoneDisconnected() {
    OPENAUTO_LOG(info) << "[AASessionController] phone disconnected";
    m_heartbeatTimer.stop();
    m_micLevelTimer.stop();
    m_firstFrameSeen.store(false, std::memory_order_relaxed);
    m_lastFrameEpochMs.store(0, std::memory_order_relaxed);

    setConnected(false);
    // App::onAndroidAutoQuit already calls waitForDevice(), so the USB
    // subsystem is still listening for a new phone plug.
    setStatus(QStringLiteral("Disconnected — replug phone"));

    // Reset the decoder so the next connection starts fresh
    m_decoder->close();
}

// Heartbeat timer fires on the GUI thread every kHeartbeatIntervalMs.
// If no frame has arrived for kHeartbeatTimeoutMs, treat it as a stale
// connection (phone screen off, cable half-seated, etc.).
void AASessionController::onHeartbeatTimeout() {
    if (!m_connected) return;

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    auto last = m_lastFrameEpochMs.load(std::memory_order_relaxed);
    if (last > 0 && (now - last) > kHeartbeatTimeoutMs) {
        OPENAUTO_LOG(warning) << "[AASessionController] frame heartbeat timeout ("
                              << (now - last) << " ms)";
        // Don't tear down — just update the UI. The phone may come back
        // (screen off then on, or brief USB glitch).
        setStatus(QStringLiteral("Connection stale — waiting for video"));
    }
}

void AASessionController::startUSB() {
    setStatus(QStringLiteral("Initializing USB..."));

    libusb_context* usbCtx = nullptr;
    libusb_init(&usbCtx);

    m_ioService = std::make_unique<boost::asio::io_service>();
    m_usbWrapper = std::make_unique<aasdk::usb::USBWrapper>(usbCtx);
    m_tcpWrapper = std::make_unique<aasdk::tcp::TCPWrapper>();

    auto queryFactory = std::make_shared<aasdk::usb::AccessoryModeQueryFactory>(
        *m_usbWrapper, *m_ioService);
    m_queryFactory = queryFactory;
    auto chainFactory = std::make_shared<aasdk::usb::AccessoryModeQueryChainFactory>(
        *m_usbWrapper, *m_ioService, *queryFactory);
    m_chainFactory = chainFactory;

    m_usbHub = std::make_shared<aasdk::usb::USBHub>(
        *m_usbWrapper, *m_ioService, *chainFactory);
    m_accessoryEnum = std::make_shared<aasdk::usb::ConnectedAccessoriesEnumerator>(
        *m_usbWrapper, *m_ioService, *chainFactory);

    auto config = std::make_shared<aa::AAConfiguration>();
    auto serviceFactory = std::make_shared<aa::AAServiceFactory>(
        *m_ioService, config, m_decoder, m_inputDevice, m_micInput);
    m_serviceFactory = serviceFactory;

    m_entityFactory = std::make_unique<f1x::openauto::autoapp::service::AndroidAutoEntityFactory>(
        *m_ioService, config, *serviceFactory);

    OPENAUTO_LOG(info) << "[AASessionController] usbHub=" << (m_usbHub ? "ok" : "null")
                       << " accessoryEnum=" << (m_accessoryEnum ? "ok" : "null");

    // Use ObservableApp so we get a callback on phone disconnect.
    try {
        auto app = std::make_shared<ObservableApp>(
            *m_ioService, *m_usbWrapper, *m_tcpWrapper, *m_entityFactory,
            m_usbHub, m_accessoryEnum);
        app->setQuitCallback([this]{
            QMetaObject::invokeMethod(this, &AASessionController::onPhoneDisconnected,
                                      Qt::QueuedConnection);
        });
        m_app = std::move(app);
    } catch (const boost::system::system_error& e) {
        OPENAUTO_LOG(error) << "[AASessionController] App init failed: " << e.what()
                            << " (port 5000 in use? kill previous instance)";
        setStatus(QString("Error: %1").arg(e.what()));
        return;
    }

    // Reset any AOAP device left over from a previous crash so the phone
    // re-enumerates and the hotplug callback fires cleanly.
    {
        libusb_device** devs = nullptr;
        auto cnt = libusb_get_device_list(usbCtx, &devs);
        for (ssize_t i = 0; i < cnt; ++i) {
            libusb_device_descriptor desc{};
            if (libusb_get_device_descriptor(devs[i], &desc) == 0 &&
                desc.idVendor == 0x18d1 &&
                (desc.idProduct == 0x2d00 || desc.idProduct == 0x2d01)) {
                libusb_device_handle* h = nullptr;
                if (libusb_open(devs[i], &h) == 0) {
                    OPENAUTO_LOG(info) << "[AASessionController] resetting stale AOAP device";
                    libusb_reset_device(h);
                    libusb_close(h);
                }
            }
        }
        if (devs) libusb_free_device_list(devs, 1);
    }

    setStatus(QStringLiteral("Scanning for devices..."));
    m_app->waitForUSBDevice();

    // Start Bluetooth handler for wireless AA discovery
    try {
        auto btService = std::make_shared<f1x::openauto::btservice::AndroidBluetoothService>();
        m_btHandler = std::make_unique<f1x::openauto::btservice::BluetoothHandler>(
            std::move(btService), config);

        // Inject WiFi credentials from command-line args for same-network fallback
        QVariant ssidVar = QCoreApplication::instance()->property("aaWifiSsid");
        QVariant pwVar = QCoreApplication::instance()->property("aaWifiPassword");
        if (ssidVar.isValid() && !ssidVar.toString().isEmpty()) {
            m_btHandler->setWifiCredentials(ssidVar.toString(), pwVar.toString());
        }

        OPENAUTO_LOG(info) << "[AASessionController] BT wireless AA handler started";
    } catch (const std::exception& e) {
        OPENAUTO_LOG(warning) << "[AASessionController] BT handler failed: " << e.what()
                              << " (wireless AA unavailable, USB still works)";
    }

    setStatus(QStringLiteral("Waiting for phone..."));

    // Install a frame callback on the decoder to detect when the phone
    // actually connects and starts sending video.  The callback fires on
    // the decoder thread; we bounce to the GUI thread via QueuedConnection.
    //
    // AAVideoItem may have already installed its own callback (for texture
    // updates) via setDecoder() during QML init.  Capture and chain it so
    // both the video display and the connection-detection logic fire.
    m_firstFrameSeen.store(false, std::memory_order_relaxed);
    auto prevCb = m_decoder->frameReadyCallback();
    m_decoder->setFrameReadyCallback([this, prevCb]{
        // Record timestamp for heartbeat monitoring (any thread).
        m_lastFrameEpochMs.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_relaxed);

        // Fire the first-frame signal exactly once per connection.
        bool expected = false;
        if (m_firstFrameSeen.compare_exchange_strong(expected, true,
                std::memory_order_acq_rel)) {
            QMetaObject::invokeMethod(this, &AASessionController::onFirstFrame,
                                      Qt::QueuedConnection);
        }

        // Chain the previous callback (AAVideoItem texture update).
        if (prevCb) prevCb();
    });

    constexpr int kIOThreads = 4;
    for (int i = 0; i < kIOThreads; ++i) {
        m_ioThreads.emplace_back([this]{ m_ioService->run(); });
    }

    constexpr int kUSBThreads = 2;
    for (int i = 0; i < kUSBThreads; ++i) {
        m_usbThreads.emplace_back([this]{
            while (!m_ioService->stopped()) {
                m_usbWrapper->handleEvents();
            }
        });
    }

    OPENAUTO_LOG(info) << "[AASessionController] USB started, "
                       << kIOThreads << " IO + " << kUSBThreads << " USB threads";
}

void AASessionController::stopUSB() {
    if (!m_ioService) return;

    OPENAUTO_LOG(info) << "[AASessionController] stopping USB";
    m_heartbeatTimer.stop();
    m_firstFrameSeen.store(false, std::memory_order_relaxed);
    m_lastFrameEpochMs.store(0, std::memory_order_relaxed);

    // 1. Stop the AA entity gracefully first — this sends a ByeBye to the
    //    phone and tears down the protocol channels before we yank the USB
    //    rug out from under it.
    if (m_app) {
        m_app->stop();
    }

    // 2. Give the strand a moment to drain the stop dispatch.
    //    Without this, io_service::stop() can preempt the entity teardown
    //    and leave USB in a half-open state.
    if (m_ioService && !m_ioService->stopped()) {
        auto deadline = std::make_shared<boost::asio::deadline_timer>(
            *m_ioService, boost::posix_time::milliseconds(200));
        // Capture `deadline` in the lambda to keep the timer alive until
        // it fires, then stop the io_service.
        deadline->async_wait([this, deadline](const boost::system::error_code&){
            m_ioService->stop();
        });
    }

    for (auto& t : m_ioThreads) if (t.joinable()) t.join();
    m_ioThreads.clear();
    for (auto& t : m_usbThreads) if (t.joinable()) t.join();
    m_usbThreads.clear();

    if (m_btHandler) { m_btHandler->shutdownService(); m_btHandler.reset(); }
    m_app.reset();
    m_entityFactory.reset();
    m_accessoryEnum.reset();
    m_usbHub.reset();
    m_serviceFactory.reset();
    m_chainFactory.reset();
    m_queryFactory.reset();
    m_tcpWrapper.reset();
    m_usbWrapper.reset();
    m_ioService.reset();

    m_decoder->close();
}
