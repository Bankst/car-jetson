#include "PipeWireAudioInput.h"
#include <f1x/openauto/Common/Log.hpp>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>

#include <cstring>

namespace aa {

// Shared with PipeWireAudioOutput — pw_init is idempotent but we guard
// with call_once for clarity.
static std::once_flag s_pwInit;

PipeWireAudioInput::PipeWireAudioInput() {
    // Ring holds 2 seconds of audio — plenty of headroom for the 100ms
    // chunks that read() returns.
    m_ringCap = kSampleRate * kFrameBytes * 2;
    m_ring.resize(m_ringCap);
}

PipeWireAudioInput::~PipeWireAudioInput() {
    m_active.store(false, std::memory_order_release);
    if (m_loop) {
        pw_thread_loop_stop(m_loop);
        if (m_stream) { pw_stream_destroy(m_stream); m_stream = nullptr; }
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
    }
}

bool PipeWireAudioInput::open() {
    std::call_once(s_pwInit, [] { pw_init(nullptr, nullptr); });

    OPENAUTO_LOG(info) << "[PipeWireAudioInput] open " << kChannels << "ch "
                       << kSampleRate << "Hz S16_LE";

    m_loop = pw_thread_loop_new("banks-aa-mic", nullptr);
    pw_thread_loop_lock(m_loop);

    static const pw_stream_events events = [] {
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.process = &PipeWireAudioInput::onProcess;
        return e;
    }();

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE,     "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE,     "Communication",
        PW_KEY_APP_NAME,       "banks-frontend",
        PW_KEY_NODE_NAME,      "banks-aa-mic",
        nullptr);

    m_stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(m_loop),
        "banks-aa-mic-capture",
        props, &events, this);

    spa_audio_info_raw info{};
    info.format   = SPA_AUDIO_FORMAT_S16_LE;
    info.rate     = kSampleRate;
    info.channels = kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_MONO;

    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    const spa_pod* params[1];
    params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

    // PW_DIRECTION_INPUT = capture from source (microphone).
    // No CAPTURE_SINK — we want the default audio source, not a sink monitor.
    pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
        params, 1);

    pw_thread_loop_unlock(m_loop);
    pw_thread_loop_start(m_loop);

    OPENAUTO_LOG(info) << "[PipeWireAudioInput] stream connected";
    return true;
}

bool PipeWireAudioInput::isActive() const {
    return m_active.load(std::memory_order_acquire);
}

void PipeWireAudioInput::start(StartPromise::Pointer promise) {
    OPENAUTO_LOG(info) << "[PipeWireAudioInput] start capture";
    m_active.store(true, std::memory_order_release);
    promise->resolve();
}

void PipeWireAudioInput::stop() {
    OPENAUTO_LOG(info) << "[PipeWireAudioInput] stop";
    m_active.store(false, std::memory_order_release);

    // Reject any parked read promise so the caller doesn't hang.
    {
        std::lock_guard lk(m_mtx);
        if (m_pendingRead) {
            m_pendingRead->reject();
            m_pendingRead.reset();
        }
    }

    // Don't destroy PipeWire stream — phone cycles start/stop repeatedly.
    // Stream stays alive for peakLevel monitoring. Destroyed in destructor.

    // Drain ring
    {
        std::lock_guard lk(m_mtx);
        m_ringHead = 0;
        m_ringTail = 0;
    }
}

void PipeWireAudioInput::read(ReadPromise::Pointer promise) {
    std::lock_guard lk(m_mtx);

    if (!m_active.load(std::memory_order_relaxed)) {
        // Not active — resolve with silence so caller doesn't stall.
        aasdk::common::Data silence(kChunkBytes, 0);
        promise->resolve(std::move(silence));
        return;
    }

    // If enough data is already buffered, resolve immediately.
    if (tryResolve(promise)) return;

    // Otherwise park the promise — onProcess will fulfill it.
    m_pendingRead = std::move(promise);
}

bool PipeWireAudioInput::tryResolve(ReadPromise::Pointer& promise) {
    // Caller must hold m_mtx.
    if (ringUsed() < kChunkBytes) return false;

    aasdk::common::Data data(kChunkBytes);
    ringRead(data.data(), kChunkBytes);
    promise->resolve(std::move(data));
    return true;
}

// ---- PipeWire RT callback ------------------------------------------------

void PipeWireAudioInput::onProcess(void* userdata) {
    auto* self = static_cast<PipeWireAudioInput*>(userdata);

    pw_buffer* b = pw_stream_dequeue_buffer(self->m_stream);
    if (!b) return;

    spa_buffer* sb = b->buffer;
    if (sb->n_datas == 0 || !sb->datas[0].data) {
        pw_stream_queue_buffer(self->m_stream, b);
        return;
    }

    const auto* src = static_cast<const uint8_t*>(sb->datas[0].data);
    uint32_t bytes = sb->datas[0].chunk->size;

    // Compute peak level for VU meter
    if (bytes >= 2) {
        auto* s16 = reinterpret_cast<const int16_t*>(src);
        uint32_t nSamples = bytes / 2;
        int16_t peak = 0;
        for (uint32_t i = 0; i < nSamples; ++i) {
            int16_t a = s16[i] < 0 ? -s16[i] : s16[i];
            if (a > peak) peak = a;
        }
        self->m_peakLevel.store(static_cast<float>(peak) / 32768.0f, std::memory_order_relaxed);
    }

    if (!self->m_active.load(std::memory_order_relaxed) || bytes == 0) {
        pw_stream_queue_buffer(self->m_stream, b);
        return;
    }

    {
        std::lock_guard lk(self->m_mtx);
        self->ringWrite(src, bytes);

        if (self->m_pendingRead && self->ringUsed() >= kChunkBytes) {
            self->tryResolve(self->m_pendingRead);
            self->m_pendingRead.reset();
        }
    }

    pw_stream_queue_buffer(self->m_stream, b);
}

// ---- Ring buffer ---------------------------------------------------------

size_t PipeWireAudioInput::ringUsed() const {
    return (m_ringHead - m_ringTail + m_ringCap) % m_ringCap;
}

void PipeWireAudioInput::ringWrite(const uint8_t* data, size_t len) {
    // If incoming data exceeds free space, advance tail (drop oldest).
    size_t free = m_ringCap - 1 - ringUsed();
    if (len > free) {
        m_ringTail = (m_ringTail + len - free) % m_ringCap;
    }
    for (size_t i = 0; i < len; ++i) {
        m_ring[m_ringHead] = data[i];
        m_ringHead = (m_ringHead + 1) % m_ringCap;
    }
}

size_t PipeWireAudioInput::ringRead(uint8_t* dst, size_t len) {
    size_t avail = ringUsed();
    size_t toRead = std::min(len, avail);
    for (size_t i = 0; i < toRead; ++i) {
        dst[i] = m_ring[m_ringTail];
        m_ringTail = (m_ringTail + 1) % m_ringCap;
    }
    return toRead;
}

}
