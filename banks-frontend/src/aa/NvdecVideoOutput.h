#pragma once

#include <f1x/openauto/autoapp/Projection/IVideoOutput.hpp>
#include <memory>

class AAVideoDecoder;

namespace aa {

class NvdecVideoOutput : public f1x::openauto::autoapp::projection::IVideoOutput {
public:
    NvdecVideoOutput(int width, int height, std::shared_ptr<AAVideoDecoder> decoder);

    bool open() override;
    bool init() override;
    void write(aasdk::messenger::Timestamp::ValueType timestamp,
               const aasdk::common::DataConstBuffer& buffer) override;
    void stop() override;

    aap_protobuf::service::media::sink::message::VideoFrameRateType getVideoFPS() const override;
    aap_protobuf::service::media::sink::message::VideoCodecResolutionType getVideoResolution() const override;
    size_t getScreenDPI() const override;
    QRect getVideoMargins() const override;

private:
    int m_width;
    int m_height;
    std::shared_ptr<AAVideoDecoder> m_decoder;
};

}
