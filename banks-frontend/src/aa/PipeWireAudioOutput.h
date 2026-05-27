#pragma once

#include <f1x/openauto/autoapp/Projection/IAudioOutput.hpp>

#include <atomic>
#include <mutex>
#include <vector>
#include <string>

struct pw_thread_loop;
struct pw_stream;

namespace aa {

class PipeWireAudioOutput : public f1x::openauto::autoapp::projection::IAudioOutput {
public:
    PipeWireAudioOutput(uint32_t channels, uint32_t sampleSize, uint32_t sampleRate,
                        const char* role);
    ~PipeWireAudioOutput();

    bool open() override;
    void write(aasdk::messenger::Timestamp::ValueType timestamp,
               const aasdk::common::DataConstBuffer& buffer) override;
    void start() override;
    void stop() override;
    void suspend() override;
    uint32_t getSampleSize() const override { return m_sampleSize; }
    uint32_t getChannelCount() const override { return m_channels; }
    uint32_t getSampleRate() const override { return m_sampleRate; }

    float peakLevel() const { return m_peakLevel.load(std::memory_order_relaxed); }
    const std::string& role() const { return m_role; }

private:
    static void onProcess(void* userdata);

    uint32_t m_channels;
    uint32_t m_sampleSize;
    uint32_t m_sampleRate;
    std::string m_role;

    pw_thread_loop* m_loop = nullptr;
    pw_stream* m_stream = nullptr;
    std::atomic<bool> m_running{false};

    std::mutex m_ringMtx;
    std::vector<uint8_t> m_ring;
    size_t m_ringHead = 0;
    size_t m_ringTail = 0;
    size_t m_ringCap = 0;

    size_t ringUsed() const;
    size_t ringFree() const;
    void ringWrite(const uint8_t* data, size_t len);
    size_t ringRead(uint8_t* dst, size_t len);

    std::atomic<float> m_peakLevel{0.0f};
    std::atomic<bool> m_logNextWrite{false};
};

}
