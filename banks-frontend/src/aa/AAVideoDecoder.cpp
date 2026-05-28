extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

#include "AAVideoDecoder.h"
#include <QDebug>

AAVideoDecoder::AAVideoDecoder() = default;

AAVideoDecoder::~AAVideoDecoder() {
    close();
}

bool AAVideoDecoder::open(int width, int height) {
    m_width = width;
    m_height = height;

    const AVCodec* codec = nullptr;
    bool hwdec = false;

#ifdef BANKS_AA_NVDEC
    codec = avcodec_find_decoder_by_name("h264_nvv4l2dec");
    if (codec) {
        hwdec = true;
        qInfo("[AAVideoDecoder] using NVDEC h264_nvv4l2dec");
    }
#endif

    if (!codec)
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        qWarning("[AAVideoDecoder] h264 decoder not found");
        return false;
    }

    m_ctx = avcodec_alloc_context3(codec);
    m_ctx->width = width;
    m_ctx->height = height;

#ifndef BANKS_AA_NVDEC
    if (av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI, "/dev/dri/renderD128", nullptr, 0) == 0) {
        m_ctx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_ctx->hwaccel_flags |= AV_HWACCEL_FLAG_ALLOW_PROFILE_MISMATCH;
        hwdec = true;
        qInfo("[AAVideoDecoder] VAAPI hw device created");
    } else {
        qInfo("[AAVideoDecoder] VAAPI not available, using software decode");
    }
#endif

    if (!hwdec)
        m_ctx->thread_count = 2;

    if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
        qWarning("[AAVideoDecoder] failed to open codec");
        if (m_hwDeviceCtx) { av_buffer_unref(&m_hwDeviceCtx); m_hwDeviceCtx = nullptr; }
        avcodec_free_context(&m_ctx);
        return false;
    }

    m_sws = nullptr;
    m_swsSrcFmt = -1;
    m_swsWidth = 0;
    m_swsHeight = 0;

    m_running = true;
    m_thread = std::thread(&AAVideoDecoder::decodeLoop, this);
    qInfo("[AAVideoDecoder] opened %dx%d %s%s", width, height, codec->name,
          hwdec ? " (hw accel)" : " (software)");
    return true;
}

void AAVideoDecoder::close() {
    if (m_running) {
        m_running = false;
        m_queueCv.notify_all();
        if (m_thread.joinable()) m_thread.join();
    }
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_swsSrcFmt = -1;
    m_swsWidth = 0;
    m_swsHeight = 0;
    if (m_ctx) { avcodec_free_context(&m_ctx); }
    if (m_hwDeviceCtx) { av_buffer_unref(&m_hwDeviceCtx); m_hwDeviceCtx = nullptr; }
}

void AAVideoDecoder::feedNalUnit(const uint8_t* data, size_t size) {
    {
        std::lock_guard lk(m_queueMtx);
        m_nalQueue.emplace_back(data, data + size);
    }
    m_queueCv.notify_one();
}

void AAVideoDecoder::setFrameReadyCallback(FrameCallback cb) {
    m_frameReady = std::move(cb);
}

std::shared_ptr<const AAVideoDecoder::Frame> AAVideoDecoder::latestFrame() const {
    return m_latestFrame.load();
}

void AAVideoDecoder::decodeLoop() {
    AVPacket* pkt = av_packet_alloc();

    while (m_running) {
        std::vector<uint8_t> nal;
        {
            std::unique_lock lk(m_queueMtx);
            m_queueCv.wait(lk, [&]{ return !m_nalQueue.empty() || !m_running; });
            if (!m_running) break;
            nal = std::move(m_nalQueue.front());
            m_nalQueue.erase(m_nalQueue.begin());
        }

        pkt->data = nal.data();
        pkt->size = static_cast<int>(nal.size());
        decodePacket(pkt);
        av_packet_unref(pkt);
    }

    // flush decoder
    decodePacket(nullptr);
    av_packet_free(&pkt);
}

bool AAVideoDecoder::decodePacket(AVPacket* pkt) {
    int ret = avcodec_send_packet(m_ctx, pkt);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) return false;

    AVFrame* frame = av_frame_alloc();
    while (avcodec_receive_frame(m_ctx, frame) == 0) {
        convertFrame(frame);
    }
    av_frame_free(&frame);
    return true;
}

void AAVideoDecoder::convertFrame(AVFrame* avf) {
    AVFrame* swFrame = nullptr;

    if (avf->format == AV_PIX_FMT_VAAPI) {
        swFrame = av_frame_alloc();
        if (av_hwframe_transfer_data(swFrame, avf, 0) < 0) {
            qWarning("[AAVideoDecoder] av_hwframe_transfer_data failed");
            av_frame_free(&swFrame);
            return;
        }
        avf = swFrame;
    }

    auto srcFmt = static_cast<AVPixelFormat>(avf->format);

    if (!m_sws || m_swsSrcFmt != avf->format
               || m_swsWidth != avf->width || m_swsHeight != avf->height) {
        if (m_sws) sws_freeContext(m_sws);
        m_sws = sws_getContext(avf->width, avf->height, srcFmt,
                               avf->width, avf->height, AV_PIX_FMT_RGBA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) {
            qWarning("[AAVideoDecoder] sws_getContext failed for fmt %d %dx%d",
                     avf->format, avf->width, avf->height);
            if (swFrame) av_frame_free(&swFrame);
            return;
        }
        m_swsSrcFmt = avf->format;
        m_swsWidth = avf->width;
        m_swsHeight = avf->height;
        qInfo("[AAVideoDecoder] sws_context: fmt=%d (%s) %dx%d",
              avf->format,
              srcFmt == AV_PIX_FMT_NV12 ? "NV12" :
              srcFmt == AV_PIX_FMT_YUV420P ? "YUV420P" : "other",
              avf->width, avf->height);
    }

    auto f = std::make_shared<Frame>();
    f->width = avf->width;
    f->height = avf->height;
    f->rgba.resize(avf->width * avf->height * 4);

    uint8_t* dst[1] = { f->rgba.data() };
    int dstStride[1] = { avf->width * 4 };
    sws_scale(m_sws, avf->data, avf->linesize, 0, avf->height, dst, dstStride);

    if (swFrame) av_frame_free(&swFrame);

    m_latestFrame.store(std::move(f));
    if (m_frameReady) m_frameReady();
}
