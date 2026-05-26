#pragma once

#include <f1x/openauto/autoapp/Service/IServiceFactory.hpp>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <memory>

class AAVideoDecoder;

namespace aa {

class QmlInputDevice;
class PipeWireAudioInput;
class PipeWireAudioOutput;

class AAServiceFactory : public f1x::openauto::autoapp::service::IServiceFactory {
public:
    AAServiceFactory(boost::asio::io_service& ioService,
                     f1x::openauto::autoapp::configuration::IConfiguration::Pointer config,
                     std::shared_ptr<AAVideoDecoder> decoder,
                     std::shared_ptr<QmlInputDevice> inputDevice,
                     std::shared_ptr<PipeWireAudioInput> micInput);

    f1x::openauto::autoapp::service::ServiceList
    create(aasdk::messenger::IMessenger::Pointer messenger) override;

private:
    boost::asio::io_service& m_ioService;
    f1x::openauto::autoapp::configuration::IConfiguration::Pointer m_config;
    std::shared_ptr<AAVideoDecoder> m_decoder;
    std::shared_ptr<QmlInputDevice> m_inputDevice;
    std::shared_ptr<PipeWireAudioInput> m_micInput;

public:
    std::shared_ptr<PipeWireAudioOutput> mediaAudio() const { return m_mediaAudio; }
    std::shared_ptr<PipeWireAudioOutput> guidanceAudio() const { return m_guidanceAudio; }
    std::shared_ptr<PipeWireAudioOutput> systemAudio() const { return m_systemAudio; }
private:
    std::shared_ptr<PipeWireAudioOutput> m_mediaAudio;
    std::shared_ptr<PipeWireAudioOutput> m_guidanceAudio;
    std::shared_ptr<PipeWireAudioOutput> m_systemAudio;
};

}
