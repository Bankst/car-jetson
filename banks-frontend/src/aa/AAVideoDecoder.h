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
    bool decodePacket(AVPacket* pkt);
    void convertFrame(AVFrame* avf);

    AVCodecContext* m_ctx = nullptr;
    AVBufferRef* m_hwDeviceCtx = nullptr;
    SwsContext* m_sws = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_swsSrcFmt = -1;   // AVPixelFormat of current sws source
    int m_swsWidth = 0;     // width  used to create current sws
    int m_swsHeight = 0;    // height used to create current sws

    std::thread m_thread;
    std::atomic<bool> m_running{false};

    mutable std::mutex m_queueMtx;
    std::condition_variable m_queueCv;
    std::vector<std::vector<uint8_t>> m_nalQueue;

    std::atomic<std::shared_ptr<const Frame>> m_latestFrame;
    FrameCallback m_frameReady;
};
