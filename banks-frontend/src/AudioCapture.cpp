#include "AudioCapture.h"
#include "Log.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>

#include <cstring>

namespace {
constexpr uint32_t kSampleRate = 44100;
constexpr uint32_t kChannels   = 1;
}

AudioCapture::AudioCapture() {
    pw_init(nullptr, nullptr);
    for (auto& s : m_consumers) s.store(nullptr, std::memory_order_relaxed);
}

AudioCapture::~AudioCapture() {
    stop();
    pw_deinit();
}

void AudioCapture::start() {
    if (m_running.load()) { qCInfo(logAudio) << "start: already running"; return; }
    qCInfo(logAudio) << "starting PipeWire capture (rate=" << kSampleRate << "ch=" << kChannels << ")";

    m_loop = pw_thread_loop_new("banks-audio", nullptr);
    pw_thread_loop_lock(m_loop);

    static const pw_stream_events events = []{
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.process = &AudioCapture::onProcess;
        return e;
    }();

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,     "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE,     "Music",
        PW_KEY_STREAM_CAPTURE_SINK, "true",
        nullptr);

    m_stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(m_loop),
        "banks-frontend-capture",
        props, &events, this);

    spa_audio_info_raw info{};
    info.format   = SPA_AUDIO_FORMAT_F32;
    info.rate     = kSampleRate;
    info.channels = kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_MONO;

    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    pw_thread_loop_unlock(m_loop);
    pw_thread_loop_start(m_loop);
    m_running.store(true);
    qCInfo(logAudio) << "PipeWire thread loop started";
}

void AudioCapture::stop() {
    if (!m_running.exchange(false)) return;
    qCInfo(logAudio) << "stopping PipeWire capture";
    if (m_loop) {
        pw_thread_loop_stop(m_loop);
        if (m_stream) { pw_stream_destroy(m_stream); m_stream = nullptr; }
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
    }
}

bool AudioCapture::addConsumer(AudioConsumer* c) {
    if (!c) return false;
    for (auto& slot : m_consumers) {
        AudioConsumer* expected = nullptr;
        if (slot.compare_exchange_strong(expected, c, std::memory_order_acq_rel)) {
            qCInfo(logAudio) << "consumer attached, total=" << consumerCount();
            return true;
        }
    }
    qCWarning(logAudio) << "addConsumer: no free slots (max=" << kMaxConsumers << ")";
    return false;
}

void AudioCapture::removeConsumer(AudioConsumer* c) {
    if (!c) return;
    for (auto& slot : m_consumers) {
        AudioConsumer* expected = c;
        if (slot.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel)) {
            qCInfo(logAudio) << "consumer detached, remaining=" << consumerCount();
            return;
        }
    }
}

int AudioCapture::consumerCount() const {
    int n = 0;
    for (auto& slot : m_consumers) {
        if (slot.load(std::memory_order_relaxed)) ++n;
    }
    return n;
}

void AudioCapture::onProcess(void* userdata) {
    auto* self = static_cast<AudioCapture*>(userdata);
    pw_buffer* b = pw_stream_dequeue_buffer(self->m_stream);
    if (!b) return;
    spa_buffer* sb = b->buffer;
    if (sb->n_datas == 0 || !sb->datas[0].data) {
        pw_stream_queue_buffer(self->m_stream, b);
        return;
    }
    const auto* src = static_cast<const float*>(sb->datas[0].data);
    uint32_t bytes = sb->datas[0].chunk->size;
    uint32_t n = bytes / sizeof(float);

    for (auto& slot : self->m_consumers) {
        AudioConsumer* c = slot.load(std::memory_order_acquire);
        if (c) c->onSamples(src, n);
    }

    pw_stream_queue_buffer(self->m_stream, b);
}
