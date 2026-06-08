#include "NvdecVideoOutput.h"
#include "AAVideoDecoder.h"
#include <f1x/openauto/Common/Log.hpp>
#include <QSettings>

namespace aa {

NvdecVideoOutput::NvdecVideoOutput(int width, int height, std::shared_ptr<AAVideoDecoder> decoder)
    : m_width(width), m_height(height), m_decoder(std::move(decoder))
{
    QSettings cfg("/etc/banks-frontend/aa.conf", QSettings::IniFormat);
    m_dpi = cfg.value("video/dpi", 140).toInt();
    m_resolution = cfg.value("video/resolution", 1080).toInt();
    OPENAUTO_LOG(info) << "[NvdecVideoOutput] config: dpi=" << m_dpi << " resolution=" << m_resolution;
}

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
    using R = aap_protobuf::service::media::sink::message::VideoCodecResolutionType;
    switch (m_resolution) {
    case 480:  return R::VIDEO_800x480;
    case 720:  return R::VIDEO_1280x720;
    case 1440: return R::VIDEO_2560x1440;
    case 2160: return R::VIDEO_3840x2160;
    default:   return R::VIDEO_1920x1080;
    }
}

size_t NvdecVideoOutput::getScreenDPI() const {
    return m_dpi;
}

QRect NvdecVideoOutput::getVideoMargins() const {
    return QRect(0, 0, 0, 0);
}

}
