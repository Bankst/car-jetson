#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>
#include <functional>

struct AVCodecContext;
struct AVBufferRef;
struct AVFrame;
struct AVPacket;
struct SwsContext;

class AAVideoDecoder {
public:
    struct Frame {
        std::vector<uint8_t> rgba;
        int width = 0;
        int height = 0;
    };

    AAVideoDecoder();
    ~AAVideoDecoder();

    bool open(int width, int height);
    void close();

    void feedNalUnit(const uint8_t* data, size_t size);

    using FrameCallback = std::function<void()>;
    void setFrameReadyCallback(FrameCallback cb);
    FrameCallback frameReadyCallback() const { return m_frameReady; }

    std::shared_ptr<const Frame> latestFrame() const;

private:
    void decodeLoop();
    void convertFrame(AVFrame* avf);

#ifndef BANKS_AA_NVDEC
    bool decodePacket(AVPacket* pkt);
    AVCodecContext* m_ctx = nullptr;
    AVBufferRef* m_hwDeviceCtx = nullptr;
#endif

#ifdef BANKS_AA_NVDEC
    static constexpr int kOutputBufCount = 4;
    static constexpr int kOutputBufSize = 2 * 1024 * 1024;
    static constexpr int kMaxCaptureBufs = 12;

    struct MmapPlane {
        void* ptr = nullptr;
        size_t length = 0;
        int fd = -1;  // DMA-buf fd from VIDIOC_EXPBUF
    };

    int m_v4l2Fd = -1;
    MmapPlane m_outputPlanes[kOutputBufCount];
    MmapPlane m_capturePlanes[kMaxCaptureBufs][2]; // NV12M: Y + UV
    int m_captureBufCount = 0;
    int m_captureWidth = 0;
    int m_captureHeight = 0;
    bool m_captureReady = false;

    bool openNvdec();
    void closeNvdec();
    void nvdecDecodeLoop();
    bool handleResolutionChange();
    void processDecodedFrame(int bufIndex);

    void* m_dstSurf = nullptr; // pitch-linear NvBufSurface for detiling
#endif

    SwsContext* m_sws = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_swsSrcFmt = -1;
    int m_swsWidth = 0;
    int m_swsHeight = 0;

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    mutable std::mutex m_queueMtx;
    std::condition_variable m_queueCv;
    std::vector<std::vector<uint8_t>> m_nalQueue;

    std::atomic<std::shared_ptr<const Frame>> m_latestFrame;
    FrameCallback m_frameReady;
};
