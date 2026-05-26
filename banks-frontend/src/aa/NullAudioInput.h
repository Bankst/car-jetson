#pragma once

#include <f1x/openauto/autoapp/Projection/IAudioInput.hpp>

namespace aa {

class NullAudioInput : public f1x::openauto::autoapp::projection::IAudioInput {
public:
    NullAudioInput(uint32_t channels, uint32_t sampleSize, uint32_t sampleRate)
        : m_channels(channels), m_sampleSize(sampleSize), m_sampleRate(sampleRate) {}

    bool open() override { return true; }
    bool isActive() const override { return m_active; }

    void read(ReadPromise::Pointer promise) override {
        aasdk::common::Data silence(m_sampleRate / 10 * m_channels * (m_sampleSize / 8), 0);
        promise->resolve(std::move(silence));
    }

    void start(StartPromise::Pointer promise) override {
        m_active = true;
        promise->resolve();
    }

    void stop() override { m_active = false; }
    uint32_t getSampleSize() const override { return m_sampleSize; }
    uint32_t getChannelCount() const override { return m_channels; }
    uint32_t getSampleRate() const override { return m_sampleRate; }

private:
    uint32_t m_channels;
    uint32_t m_sampleSize;
    uint32_t m_sampleRate;
    bool m_active = false;
};

}
