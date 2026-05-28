#pragma once

#include "AudioCapture.h"
#include <atomic>
#include <vector>
#include <cstddef>

// Simple SPSC ring-buffer audio consumer. The PipeWire RT thread writes
// via onSamples(); a single consumer thread (Qt render or GUI thread)
// drains via pop().
class AudioRingConsumer : public AudioConsumer {
public:
    explicit AudioRingConsumer(size_t ringSize)
        : m_ring(ringSize, 0.0f), m_size(ringSize) {}

    void onSamples(const float* data, size_t n) override {
        size_t w = m_writePos.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
            m_ring[(w + i) % m_size] = data[i];
        }
        m_writePos.store(w + n, std::memory_order_release);
    }

    size_t pop(float* out, size_t maxSamples) {
        size_t w = m_writePos.load(std::memory_order_acquire);
        size_t r = m_readPos.load(std::memory_order_relaxed);
        size_t avail = w - r;
        // Avoid wrap-around lag: if more than ring's worth, jump forward.
        if (avail > m_size) {
            r = w - m_size;
            avail = m_size;
        }
        size_t n = avail < maxSamples ? avail : maxSamples;
        for (size_t i = 0; i < n; ++i) out[i] = m_ring[(r + i) % m_size];
        m_readPos.store(r + n, std::memory_order_release);
        return n;
    }

    // Peek latest N samples without consuming. Returns count copied.
    size_t peekLatest(float* out, size_t maxSamples) const {
        size_t w = m_writePos.load(std::memory_order_acquire);
        size_t avail = w;
        if (avail > m_size) avail = m_size;
        size_t n = avail < maxSamples ? avail : maxSamples;
        size_t start = w - n;
        for (size_t i = 0; i < n; ++i) out[i] = m_ring[(start + i) % m_size];
        return n;
    }

private:
    std::vector<float> m_ring;
    size_t m_size;
    std::atomic<size_t> m_writePos{0};
    std::atomic<size_t> m_readPos{0};
};
