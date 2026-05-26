#include "NvdecVideoOutput.h"
#include "AAVideoDecoder.h"
#include <f1x/openauto/Common/Log.hpp>

namespace aa {

NvdecVideoOutput::NvdecVideoOutput(int width, int height, std::shared_ptr<AAVideoDecoder> decoder)
    : m_width(width), m_height(height), m_decoder(std::move(decoder)) {}

bool NvdecVideoOutput::open() {
    OPENAUTO_LOG(info) << "[NvdecVideoOutput] open " << m_width << "x" << m_height;
    return m_decoder->open(m_width, m_height);
}

bool NvdecVideoOutput::init() {
    OPENAUTO_LOG(info) << "[NvdecVideoOutput] init";
    return true;
}

void NvdecVideoOutput::write(aasdk::messenger::Timestamp::ValueType,
                              const aasdk::common::DataConstBuffer& buffer) {
    m_decoder->feedNalUnit(buffer.cdata, buffer.size);
}

void NvdecVideoOutput::stop() {
    OPENAUTO_LOG(info) << "[NvdecVideoOutput] stop";
    m_decoder->close();
}

aap_protobuf::service::media::sink::message::VideoFrameRateType
NvdecVideoOutput::getVideoFPS() const {
    return aap_protobuf::service::media::sink::message::VideoFrameRateType::VIDEO_FPS_60;
}

aap_protobuf::service::media::sink::message::VideoCodecResolutionType
NvdecVideoOutput::getVideoResolution() const {
    return aap_protobuf::service::media::sink::message::VideoCodecResolutionType::VIDEO_1920x1080;
}

size_t NvdecVideoOutput::getScreenDPI() const {
    return 140;
}

QRect NvdecVideoOutput::getVideoMargins() const {
    return QRect(0, 0, 0, 0);
}

}
