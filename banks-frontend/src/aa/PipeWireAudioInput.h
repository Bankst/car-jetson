#pragma once

#include <f1x/openauto/autoapp/Projection/IAudioInput.hpp>

#include <atomic>
#include <mutex>
#include <vector>

struct pw_thread_loop;
struct pw_stream;

namespace aa {

// PipeWire-backed microphone capture for Android Auto (Google Assistant).
// Captures mono S16_LE at 16000 Hz from the default PipeWire audio source.
//
// Bridges PipeWire's push model (onProcess callback when samples arrive)
// with openauto's pull model (read() returns a promise resolved with data):
//   - onProcess writes captured samples into a ring buffer
//   - read() resolves immediately if data is available, otherwise parks
//     the promise for the next onProcess to fulfill
class PipeWireAudioInput : public f1x::openauto::autoapp::projection::IAudioInput {
public:
    PipeWireAudioInput();
    ~PipeWireAudioInput();

    bool open() override;
    bool isActive() const override;
    void read(ReadPromise::Pointer promise) override;
    void start(StartPromise::Pointer promise) override;
    void stop() override;
    uint32_t getSampleSize() const override { return kSampleSize; }
    uint32_t getChannelCount() const override { return kChannels; }
    uint32_t getSampleRate() const override { return kSampleRate; }

private:
    static constexpr uint32_t kSampleRate  = 16000;
    static constexpr uint32_t kChannels    = 1;
    static constexpr uint32_t kSampleSize  = 16;  // bits per sample
    static constexpr uint32_t kFrameBytes  = kChannels * (kSampleSize / 8);
    // Chunk size per read() — 100ms of audio (matches NullAudioInput)
    static constexpr size_t   kChunkBytes  = kSampleRate / 10 * kFrameBytes;

    static void onProcess(void* userdata);

    // Try to drain kChunkBytes from ring into an aasdk::common::Data and
    // resolve the given promise. Returns true if resolved.
    bool tryResolve(ReadPromise::Pointer& promise);

    pw_thread_loop* m_loop = nullptr;
    pw_stream*      m_stream = nullptr;
    std::atomic<bool> m_active{false};

    // Protects ring buffer state AND the pending promise pointer.
    // Acquired briefly from both the PW RT thread and the openauto strand.
    std::mutex m_mtx;

    // Simple circular byte buffer
    std::vector<uint8_t> m_ring;
    size_t m_ringHead = 0;
    size_t m_ringTail = 0;
    size_t m_ringCap  = 0;

    size_t ringUsed() const;
    void   ringWrite(const uint8_t* data, size_t len);
    size_t ringRead(uint8_t* dst, size_t len);

    // Promise parked by read() when the ring has insufficient data.
    // Fulfilled by the next onProcess that accumulates enough samples.
    ReadPromise::Pointer m_pendingRead;

    // Peak level from last onProcess (0.0–1.0), readable from any thread.
    std::atomic<float> m_peakLevel{0.0f};
public:
    float peakLevel() const { return m_peakLevel.load(std::memory_order_relaxed); }
};

}
