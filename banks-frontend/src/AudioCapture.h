#pragma once

#include <atomic>
#include <array>
#include <cstddef>

struct pw_thread_loop;
struct pw_stream;

// Realtime-safe consumer interface. onSamples() is called on the
// PipeWire realtime thread; implementations MUST be lock-free and
// non-allocating.
class AudioConsumer {
public:
    virtual ~AudioConsumer() = default;
    virtual void onSamples(const float* data, size_t n) = 0;
};

// Single PipeWire capture source. Mono float @ 44100 Hz.
// Fans out samples to up to kMaxConsumers consumers via a fixed-size
// atomic slot array — no locking on the RT thread.
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();

    void start();
    void stop();

    // Returns false if consumer slots are full.
    bool addConsumer(AudioConsumer* c);
    void removeConsumer(AudioConsumer* c);

    int consumerCount() const;

    static constexpr size_t kMaxConsumers = 4;

private:
    static void onProcess(void* userdata);

    pw_thread_loop* m_loop = nullptr;
    pw_stream*      m_stream = nullptr;
    std::atomic<bool> m_running{false};

    std::array<std::atomic<AudioConsumer*>, kMaxConsumers> m_consumers{};
};
