#include "AAServiceFactory.h"
#include "NvdecVideoOutput.h"
#include "NullAudioOutput.h"
#include "PipeWireAudioOutput.h"
#include "NullAudioInput.h"
#include "PipeWireAudioInput.h"
#include "QmlInputDevice.h"
#include "AAVideoDecoder.h"

#include <aasdk/Channel/MediaSink/Video/Channel/VideoChannel.hpp>
#include <aasdk/Channel/MediaSink/Audio/Channel/MediaAudioChannel.hpp>
#include <aasdk/Channel/MediaSink/Audio/Channel/SystemAudioChannel.hpp>
#include <aasdk/Channel/MediaSink/Audio/Channel/GuidanceAudioChannel.hpp>

#include <f1x/openauto/autoapp/Service/MediaSink/VideoService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/MediaAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/SystemAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSink/GuidanceAudioService.hpp>
#include <f1x/openauto/autoapp/Service/MediaSource/MicrophoneMediaSourceService.hpp>
#include <f1x/openauto/autoapp/Service/InputSource/InputSourceService.hpp>
#include <f1x/openauto/autoapp/Service/Sensor/SensorService.hpp>
#include <f1x/openauto/autoapp/Projection/DummyBluetoothDevice.hpp>
#include <f1x/openauto/autoapp/Projection/LocalBluetoothDevice.hpp>
#include <f1x/openauto/autoapp/Service/Bluetooth/BluetoothService.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace aa {

using namespace f1x::openauto::autoapp;

AAServiceFactory::AAServiceFactory(boost::asio::io_service& ioService,
                                   configuration::IConfiguration::Pointer config,
                                   std::shared_ptr<AAVideoDecoder> decoder,
                                   std::shared_ptr<QmlInputDevice> inputDevice,
                                   std::shared_ptr<PipeWireAudioInput> micInput)
    : m_ioService(ioService)
    , m_config(std::move(config))
    , m_decoder(std::move(decoder))
    , m_inputDevice(std::move(inputDevice))
    , m_micInput(std::move(micInput)) {}

service::ServiceList AAServiceFactory::create(aasdk::messenger::IMessenger::Pointer messenger) {
    OPENAUTO_LOG(info) << "[AAServiceFactory] create()";
    service::ServiceList services;

    // Video
    auto videoOutput = std::make_shared<NvdecVideoOutput>(1920, 1080, m_decoder);
    services.emplace_back(
        std::make_shared<service::mediasink::VideoService>(m_ioService, messenger, std::move(videoOutput)));

    // Audio — PipeWire playback streams with per-channel media roles
    m_mediaAudio = std::make_shared<PipeWireAudioOutput>(2, 16, 48000, "Music");
    services.emplace_back(
        std::make_shared<service::mediasink::MediaAudioService>(m_ioService, messenger, m_mediaAudio));

    m_guidanceAudio = std::make_shared<PipeWireAudioOutput>(1, 16, 16000, "Navigation");
    services.emplace_back(
        std::make_shared<service::mediasink::GuidanceAudioService>(m_ioService, messenger, m_guidanceAudio));

    m_systemAudio = std::make_shared<PipeWireAudioOutput>(1, 16, 16000, "Notification");
    services.emplace_back(
        std::make_shared<service::mediasink::SystemAudioService>(m_ioService, messenger, m_systemAudio));

    // Mic — PipeWire capture from default audio source
    projection::IAudioInput::Pointer micInput = m_micInput;
    services.emplace_back(
        std::make_shared<service::mediasource::MicrophoneMediaSourceService>(m_ioService, messenger, std::move(micInput)));

    // Input
    services.emplace_back(
        std::make_shared<service::inputsource::InputSourceService>(m_ioService, messenger, m_inputDevice));

    // Sensor
    services.emplace_back(
        std::make_shared<service::sensor::SensorService>(m_ioService, messenger));

    // Bluetooth — use real adapter if available, else dummy
    projection::IBluetoothDevice::Pointer btDevice;
    auto btAddr = m_config->getBluetoothAdapterAddress();
    if (!btAddr.empty()) {
        OPENAUTO_LOG(info) << "[AAServiceFactory] using local BT adapter: " << btAddr;
        btDevice = projection::IBluetoothDevice::Pointer(
            new projection::LocalBluetoothDevice(QString::fromStdString(btAddr)),
            [](projection::IBluetoothDevice* p){ static_cast<QObject*>(static_cast<projection::LocalBluetoothDevice*>(p))->deleteLater(); });
    } else {
        auto* local = new projection::LocalBluetoothDevice();
        if (local->isAvailable()) {
            OPENAUTO_LOG(info) << "[AAServiceFactory] using default BT adapter: " << local->getAdapterAddress();
            btDevice = projection::IBluetoothDevice::Pointer(local,
                [](projection::IBluetoothDevice* p){ static_cast<QObject*>(static_cast<projection::LocalBluetoothDevice*>(p))->deleteLater(); });
        } else {
            OPENAUTO_LOG(info) << "[AAServiceFactory] no BT adapter, using dummy";
            delete local;
            btDevice = std::make_shared<projection::DummyBluetoothDevice>();
        }
    }
    services.emplace_back(
        std::make_shared<service::bluetooth::BluetoothService>(m_ioService, messenger, std::move(btDevice)));

    return services;
}

}
