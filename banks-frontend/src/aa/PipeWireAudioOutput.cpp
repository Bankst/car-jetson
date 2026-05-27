#include "PipeWireAudioOutput.h"
#include <f1x/openauto/Common/Log.hpp>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>

#include <cstring>
#include <mutex>

namespace aa {

static std::once_flag s_pwInit;

PipeWireAudioOutput::PipeWireAudioOutput(uint32_t channels, uint32_t sampleSize,
                                         uint32_t sampleRate, const char* role)
    : m_channels(channels), m_sampleSize(sampleSize), m_sampleRate(sampleRate), m_role(role)
{
    m_ringCap = sampleRate * channels * (sampleSize / 8) * 350 / 1000;
    m_ring.resize(m_ringCap);
}

PipeWireAudioOutput::~PipeWireAudioOutput() {
    stop();
}

bool PipeWireAudioOutput::open() {
    std::call_once(s_pwInit, []{ pw_init(nullptr, nullptr); });

    OPENAUTO_LOG(info) << "[PipeWireAudioOutput] open " << m_channels << "ch "
                       << m_sampleRate << "Hz role=" << m_role;

    m_loop = pw_thread_loop_new("banks-aa-audio", nullptr);
    pw_thread_loop_lock(m_loop);

    static const pw_stream_events events = []{
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.process = &PipeWireAudioOutput::onProcess;
        return e;
    }();

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,     "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_MEDIA_ROLE,     m_role.c_str(),
        PW_KEY_APP_NAME,       "banks-frontend",
        PW_KEY_NODE_NAME,      (std::string("banks-aa-") + m_role).c_str(),
        nullptr);

    m_stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(m_loop),
        "banks-aa-playback",
        props, &events, this);

    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_S16_LE;
    info.rate = m_sampleRate;
    info.channels = m_channels;
    if (m_channels == 1) {
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;
    } else {
        info.position[0] = SPA_AUDIO_CHANNEL_FL;
        info.position[1] = SPA_AUDIO_CHANNEL_FR;
    }

    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    pw_stream_connect(m_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    pw_thread_loop_unlock(m_loop);
    pw_thread_loop_start(m_loop);
    m_running = true;

    OPENAUTO_LOG(info) << "[PipeWireAudioOutput] stream started";
    return true;
}

void PipeWireAudioOutput::write(aasdk::messenger::Timestamp::ValueType,
                                 const aasdk::common::DataConstBuffer& buffer) {
    if (!m_running) return;
    if (m_logNextWrite.exchange(false, std::memory_order_relaxed)) {
        OPENAUTO_LOG(info) << "[PipeWireAudioOutput:" << m_role << "] first write after start, "
                           << buffer.size << " bytes";
    }

    if (buffer.size >= 2) {
        auto* s16 = reinterpret_cast<const int16_t*>(buffer.cdata);
        uint32_t n = buffer.size / 2;
        int16_t peak = 0;
        for (uint32_t i = 0; i < n; ++i) {
            int16_t a = s16[i] < 0 ? static_cast<int16_t>(-s16[i]) : s16[i];
            if (a > peak) peak = a;
        }
        m_peakLevel.store(static_cast<float>(peak) / 32768.0f, std::memory_order_relaxed);
    }

    std::lock_guard lk(m_ringMtx);
    ringWrite(buffer.cdata, buffer.size);
}

void PipeWireAudioOutput::start() {
    OPENAUTO_LOG(info) << "[PipeWireAudioOutput:" << m_role << "] start — uncork + flush ring";
    m_logNextWrite.store(true, std::memory_order_relaxed);
    if (m_loop && m_stream) {
        pw_thread_loop_lock(m_loop);
        pw_stream_set_active(m_stream, true);
        pw_thread_loop_unlock(m_loop);
    }
    {
        std::lock_guard lk(m_ringMtx);
        m_ringHead = m_ringTail = 0;
    }
}

void PipeWireAudioOutput::stop() {
    if (!m_running.exchange(false)) return;
    OPENAUTO_LOG(info) << "[PipeWireAudioOutput] stop";
    if (m_loop) {
        pw_thread_loop_stop(m_loop);
        if (m_stream) { pw_stream_destroy(m_stream); m_stream = nullptr; }
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
    }
}

void PipeWireAudioOutput::suspend() {
    OPENAUTO_LOG(info) << "[PipeWireAudioOutput:" << m_role << "] suspend — cork";
    if (m_loop && m_stream) {
        pw_thread_loop_lock(m_loop);
        pw_stream_set_active(m_stream, false);
        pw_thread_loop_unlock(m_loop);
    }
}

void PipeWireAudioOutput::onProcess(void* userdata) {
    auto* self = static_cast<PipeWireAudioOutput*>(userdata);
    pw_buffer* b = pw_stream_dequeue_buffer(self->m_stream);
    if (!b) return;

    spa_buffer* sb = b->buffer;
    if (sb->n_datas == 0 || !sb->datas[0].data) {
        pw_stream_queue_buffer(self->m_stream, b);
        return;
    }

    auto* dst = static_cast<uint8_t*>(sb->datas[0].data);
    uint32_t maxBytes = sb->datas[0].maxsize;

    size_t got;
    {
        std::lock_guard lk(self->m_ringMtx);
        got = self->ringRead(dst, maxBytes);
    }

    if (got < maxBytes) {
        std::memset(dst + got, 0, maxBytes - got);
    }

    sb->datas[0].chunk->offset = 0;
    sb->datas[0].chunk->stride = self->m_channels * (self->m_sampleSize / 8);
    sb->datas[0].chunk->size = maxBytes;

    pw_stream_queue_buffer(self->m_stream, b);
}

size_t PipeWireAudioOutput::ringUsed() const {
    return (m_ringHead - m_ringTail + m_ringCap) % m_ringCap;
}

size_t PipeWireAudioOutput::ringFree() const {
    return m_ringCap - 1 - ringUsed();
}

void PipeWireAudioOutput::ringWrite(const uint8_t* data, size_t len) {
    if (len > ringFree()) {
        m_ringTail = (m_ringTail + len - ringFree()) % m_ringCap;
    }
    for (size_t i = 0; i < len; ++i) {
        m_ring[m_ringHead] = data[i];
        m_ringHead = (m_ringHead + 1) % m_ringCap;
    }
}

size_t PipeWireAudioOutput::ringRead(uint8_t* dst, size_t len) {
    size_t avail = ringUsed();
    size_t toRead = std::min(len, avail);
    for (size_t i = 0; i < toRead; ++i) {
        dst[i] = m_ring[m_ringTail];
        m_ringTail = (m_ringTail + 1) % m_ringCap;
    }
    return toRead;
}

}
