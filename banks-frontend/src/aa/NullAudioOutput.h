#pragma once

#include <f1x/openauto/autoapp/Projection/IAudioOutput.hpp>

namespace aa {

class NullAudioOutput : public f1x::openauto::autoapp::projection::IAudioOutput {
public:
    NullAudioOutput(uint32_t channels, uint32_t sampleSize, uint32_t sampleRate)
        : m_channels(channels), m_sampleSize(sampleSize), m_sampleRate(sampleRate) {}

    bool open() override { return true; }
    void write(aasdk::messenger::Timestamp::ValueType, const aasdk::common::DataConstBuffer&) override {}
    void start() override {}
    void stop() override {}
    void suspend() override {}
    uint32_t getSampleSize() const override { return m_sampleSize; }
    uint32_t getChannelCount() const override { return m_channels; }
    uint32_t getSampleRate() const override { return m_sampleRate; }

private:
    uint32_t m_channels;
    uint32_t m_sampleSize;
    uint32_t m_sampleRate;
};

}
