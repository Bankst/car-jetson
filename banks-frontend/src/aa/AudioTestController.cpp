#include "AudioTestController.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>

#include <QProcess>
#include <QFile>
#include <QDebug>
#include <cmath>
#include <cstring>
#include <atomic>
#include <mutex>

namespace {

struct MicProbe {
    pw_thread_loop* loop = nullptr;
    pw_stream* stream = nullptr;
    std::atomic<float> peak{0.0f};
    std::atomic<bool> running{false};

    static void onProcess(void* ud) {
        auto* self = static_cast<MicProbe*>(ud);
        pw_buffer* b = pw_stream_dequeue_buffer(self->stream);
        if (!b) return;
        spa_buffer* sb = b->buffer;
        if (sb->n_datas == 0 || !sb->datas[0].data || sb->datas[0].chunk->size < 2) {
            pw_stream_queue_buffer(self->stream, b);
            return;
        }
        auto* s16 = reinterpret_cast<const int16_t*>(sb->datas[0].data);
        uint32_t n = sb->datas[0].chunk->size / 2;
        int16_t pk = 0;
        for (uint32_t i = 0; i < n; ++i) {
            int16_t a = s16[i] < 0 ? static_cast<int16_t>(-s16[i]) : s16[i];
            if (a > pk) pk = a;
        }
        self->peak.store(static_cast<float>(pk) / 32768.0f, std::memory_order_relaxed);
        pw_stream_queue_buffer(self->stream, b);
    }

    void start() {
        if (running) return;
        static std::once_flag pwInit;
        std::call_once(pwInit, []{ pw_init(nullptr, nullptr); });

        loop = pw_thread_loop_new("banks-audio-test", nullptr);
        pw_thread_loop_lock(loop);

        static const pw_stream_events ev = []{
            pw_stream_events e{}; e.version = PW_VERSION_STREAM_EVENTS;
            e.process = &MicProbe::onProcess; return e;
        }();

        auto* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Communication",
            PW_KEY_NODE_NAME, "banks-audio-test-mic",
            nullptr);

        stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop),
            "banks-audio-test-mic", props, &ev, this);

        spa_audio_info_raw info{};
        info.format = SPA_AUDIO_FORMAT_S16_LE;
        info.rate = 16000;
        info.channels = 1;
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;

        uint8_t buf[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
        const spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

        pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
            static_cast<pw_stream_flags>(
                PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
            params, 1);

        pw_thread_loop_unlock(loop);
        pw_thread_loop_start(loop);
        running = true;
    }

    void stop() {
        if (!running.exchange(false)) return;
        if (loop) {
            pw_thread_loop_stop(loop);
            if (stream) { pw_stream_destroy(stream); stream = nullptr; }
            pw_thread_loop_destroy(loop); loop = nullptr;
        }
    }
};

static MicProbe s_mic;

}

AudioTestController::AudioTestController(QObject* parent)
    : QObject(parent)
{
    s_mic.start();
    m_pollTimer.setInterval(50);
    connect(&m_pollTimer, &QTimer::timeout, this, &AudioTestController::pollMicLevel);
    m_pollTimer.start();
}

void AudioTestController::pollMicLevel() {
    float raw = s_mic.peak.load(std::memory_order_relaxed);
    // Noise gate: ignore signal below 3% (mic noise floor)
    if (raw < 0.03f) raw = 0.0f;
    // Decay: peak drops ~20dB/s when no new samples push it up
    float lvl = raw > m_micLevel ? raw : m_micLevel * 0.85f;
    if (lvl < 0.005f) lvl = 0.0f;
    if (lvl != m_micLevel) {
        m_micLevel = lvl;
        emit micLevelChanged();
    }
    // Reset atomic so next poll detects if onProcess fired
    s_mic.peak.store(0.0f, std::memory_order_relaxed);
}

void AudioTestController::setMasterVolume(float vol) {
    QProcess::startDetached("wpctl", {"set-volume", "@DEFAULT_AUDIO_SINK@", QString::number(vol)});
    setStatusText(QString("Master volume: %1%").arg(qRound(vol * 100)));
}

void AudioTestController::setMicGain(float gain) {
    QProcess::startDetached("wpctl", {"set-volume", "@DEFAULT_AUDIO_SOURCE@", QString::number(gain)});
    setStatusText(QString("Mic gain: %1%").arg(qRound(gain * 100)));
}

void AudioTestController::testSpeakers() {
    QProcess::startDetached("pw-play", {"--target=@DEFAULT_AUDIO_SINK@", "/usr/share/sounds/freedesktop/stereo/bell.oga"});
    setStatusText("Playing test tone...");
}

void AudioTestController::testMicLoopback() {
    setStatusText("Loopback: not implemented yet");
}

void AudioTestController::recordMic(int seconds) {
    QStringList args = {"--target=@DEFAULT_AUDIO_SOURCE@", "--format=s16", "--rate=16000", "--channels=1",
                        QString("--sec=%1").arg(seconds), "/tmp/banks-mic-test.wav"};
    QProcess::startDetached("pw-record", args);
    setStatusText(QString("Recording %1s to /tmp/banks-mic-test.wav...").arg(seconds));
}

void AudioTestController::playRecording() {
    QProcess::startDetached("pw-play", {"/tmp/banks-mic-test.wav"});
    setStatusText("Playing recording...");
}

float AudioTestController::channelLevel(int) {
    return 0.0f;
}

static QString ensureToneWav(int freq, int sampleRate, int channels) {
    QString path = QString("/tmp/banks-test-tone-%1-%2-%3.wav").arg(freq).arg(sampleRate).arg(channels);
    if (QFile::exists(path)) return path;

    const int durationMs = 1000;
    const int numSamples = sampleRate * durationMs / 1000;
    const int numFrames = numSamples;
    const int dataSize = numFrames * channels * 2; // S16_LE

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {};

    // WAV header
    auto writeU32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto writeU16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };

    f.write("RIFF", 4);
    writeU32(36 + dataSize);
    f.write("WAVEfmt ", 8);
    writeU32(16);                              // chunk size
    writeU16(1);                               // PCM
    writeU16(static_cast<uint16_t>(channels));
    writeU32(static_cast<uint32_t>(sampleRate));
    writeU32(static_cast<uint32_t>(sampleRate * channels * 2)); // byte rate
    writeU16(static_cast<uint16_t>(channels * 2));              // block align
    writeU16(16);                              // bits per sample
    f.write("data", 4);
    writeU32(dataSize);

    // Generate sine with 10ms fade in/out to avoid click
    const int fadeSamples = sampleRate / 100; // 10ms
    for (int i = 0; i < numFrames; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        double sample = std::sin(2.0 * M_PI * freq * t) * 0.6;
        // Fade envelope
        if (i < fadeSamples)
            sample *= static_cast<double>(i) / fadeSamples;
        else if (i > numFrames - fadeSamples)
            sample *= static_cast<double>(numFrames - i) / fadeSamples;
        auto s16 = static_cast<int16_t>(sample * 32767.0);
        for (int c = 0; c < channels; ++c)
            f.write(reinterpret_cast<const char*>(&s16), 2);
    }
    f.close();
    return path;
}

void AudioTestController::testChannel(int ch) {
    int freq, rate, channels;
    QString label;
    switch (ch) {
    case 0: freq = 440;  rate = 48000; channels = 2; label = "Music"; break;
    case 1: freq = 880;  rate = 16000; channels = 1; label = "Navigation"; break;
    case 2: freq = 1200; rate = 16000; channels = 1; label = "System"; break;
    default: return;
    }
    QString wav = ensureToneWav(freq, rate, channels);
    if (wav.isEmpty()) {
        setStatusText("Failed to create test tone");
        return;
    }
    QProcess::startDetached("pw-play", {"--target=@DEFAULT_AUDIO_SINK@", wav});
    setStatusText(QString("Test tone: %1 Hz (%2)").arg(freq).arg(label));
}

QString AudioTestController::pipewireInfo() const {
    QProcess proc;
    proc.start("pw-cli", {"info", "0"});
    proc.waitForFinished(2000);
    return QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
}

void AudioTestController::setStatusText(const QString& s) {
    m_statusText = s;
    emit statusTextChanged();
}
