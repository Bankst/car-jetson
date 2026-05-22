#include "audio_capture.h"
#include <cstdio>
#include <cstring>
#include <cerrno>

static void on_process(void* userdata) {
    auto* cap = static_cast<AudioCapture*>(userdata);

    struct pw_buffer* b = pw_stream_dequeue_buffer(cap->stream);
    if (!b) return;

    struct spa_buffer* buf = b->buffer;
    if (!buf->datas[0].data) {
        pw_stream_queue_buffer(cap->stream, b);
        return;
    }

    auto* samples = static_cast<float*>(buf->datas[0].data);
    uint32_t n_bytes = buf->datas[0].chunk->size;
    uint32_t n_frames = n_bytes / (sizeof(float) * 2);

    if (n_frames > 0 && cap->pm)
        projectm_pcm_add_float(cap->pm, samples, n_frames, PROJECTM_STEREO);

    pw_stream_queue_buffer(cap->stream, b);
}

static const struct pw_stream_events stream_events = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

bool AudioCapture::init(projectm_handle pm_handle) {
    pm = pm_handle;

    loop = pw_thread_loop_new("banks-viz-audio", nullptr);
    if (!loop) {
        fprintf(stderr, "pw_thread_loop_new failed\n");
        return false;
    }

    ctx = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    if (!ctx) {
        fprintf(stderr, "pw_context_new failed\n");
        return false;
    }

    core = pw_context_connect(ctx, nullptr, 0);
    if (!core) {
        fprintf(stderr, "pw_context_connect failed\n");
        return false;
    }

    auto* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "DSP",
        PW_KEY_STREAM_CAPTURE_SINK, "true",
        nullptr);

    stream = pw_stream_new(core, "banks-viz-capture", props);
    if (!stream) {
        fprintf(stderr, "pw_stream_new failed\n");
        return false;
    }

    pw_stream_add_listener(stream, &listener, &stream_events, this);

    uint8_t param_buf[1024];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(param_buf, sizeof(param_buf));
    const struct spa_pod* params[1];

    struct spa_audio_info_raw info = {};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.channels = 2;
    info.rate = 44100;

    params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

    int ret = pw_stream_connect(stream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        params, 1);

    if (ret < 0) {
        fprintf(stderr, "pw_stream_connect: %s\n", strerror(-ret));
        return false;
    }

    return true;
}

void AudioCapture::start() {
    if (loop && !is_running) {
        pw_thread_loop_start(loop);
        is_running = true;
    }
}

void AudioCapture::stop() {
    if (is_running) {
        pw_thread_loop_stop(loop);
        is_running = false;
    }
    if (stream) { pw_stream_destroy(stream); stream = nullptr; }
    if (core) { pw_core_disconnect(core); core = nullptr; }
    if (ctx) { pw_context_destroy(ctx); ctx = nullptr; }
    if (loop) { pw_thread_loop_destroy(loop); loop = nullptr; }
}
