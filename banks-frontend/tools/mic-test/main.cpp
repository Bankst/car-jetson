// mic-test: standalone Qt6 Quick + PipeWire mic capture test tool
// Single-file: enumerates PipeWire audio sources, captures 16kHz mono S16_LE,
// shows live VU meter, records 3s WAV, plays it back.

#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QRegularExpression>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>
#include <spa/pod/builder.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

// ---------------------------------------------------------------------------
// WAV header for 16kHz mono S16_LE
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]      = {'R','I','F','F'};
    uint32_t chunkSize     = 0;
    char     wave[4]      = {'W','A','V','E'};
    char     fmt[4]       = {'f','m','t',' '};
    uint32_t subchunk1Size = 16;
    uint16_t audioFormat   = 1;       // PCM
    uint16_t numChannels   = 1;
    uint32_t sampleRate    = 16000;
    uint32_t byteRate      = 32000;   // 16000 * 1 * 2
    uint16_t blockAlign    = 2;       // 1 * 2
    uint16_t bitsPerSample = 16;
    char     data[4]      = {'d','a','t','a'};
    uint32_t dataSize      = 0;
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// MicTest QObject -- bridge between PipeWire and QML
// ---------------------------------------------------------------------------
class MicTest : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY sourcesChanged)
    Q_PROPERTY(int currentSourceIndex READ currentSourceIndex WRITE setCurrentSourceIndex NOTIFY currentSourceIndexChanged)
    Q_PROPERTY(float peak READ peak NOTIFY levelsChanged)
    Q_PROPERTY(float rms READ rms NOTIFY levelsChanged)
    Q_PROPERTY(float peakPercent READ peakPercent NOTIFY levelsChanged)
    Q_PROPERTY(float rmsPercent READ rmsPercent NOTIFY levelsChanged)
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit MicTest(QObject* parent = nullptr) : QObject(parent) {
        // UI refresh timer -- read atomic levels and push to QML
        m_uiTimer = new QTimer(this);
        m_uiTimer->setInterval(30);
        connect(m_uiTimer, &QTimer::timeout, this, [this] {
            emit levelsChanged();
            // Check if recording finished (signaled from RT thread)
            if (m_recJustFinished.exchange(false, std::memory_order_acq_rel)) {
                m_recording = false;
                emit recordingChanged();
                setStatus("Recorded to /tmp/pw-mic-test.wav");
            }
        });
        m_uiTimer->start();
    }

    ~MicTest() override { stopCapture(); }

    // -- Properties --
    QVariantList sources() const { return m_sources; }
    int currentSourceIndex() const { return m_currentSourceIndex; }
    float peak() const { return m_peak.load(std::memory_order_relaxed); }
    float rms() const { return m_rms.load(std::memory_order_relaxed); }
    float peakPercent() const { return peak() * 100.0f; }
    float rmsPercent() const { return rms() * 100.0f; }
    bool capturing() const { return m_capturing; }
    bool recording() const { return m_recording; }
    bool playing() const { return m_playing; }
    QString statusText() const { return m_statusText; }

    void setCurrentSourceIndex(int idx) {
        if (idx == m_currentSourceIndex) return;
        m_currentSourceIndex = idx;
        emit currentSourceIndexChanged();
        // Restart capture on the new source
        if (m_capturing) {
            stopCapture();
            startCapture();
        }
    }

    // -- Slots callable from QML --
    Q_INVOKABLE void refreshSources() {
        m_sources.clear();
        // Use pw-cli to enumerate audio source nodes
        QProcess proc;
        proc.start("pw-cli", {"ls", "Node"});
        proc.waitForFinished(3000);
        QString out = proc.readAllStandardOutput();

        // Split on lines starting with "\tid " — each node block begins there.
        QStringList blocks;
        QString current;
        for (const auto& line : out.split('\n')) {
            if (line.contains(QRegularExpression("^\\s*id \\d+,"))) {
                if (!current.isEmpty()) blocks.append(current);
                current = line + "\n";
            } else {
                current += line + "\n";
            }
        }
        if (!current.isEmpty()) blocks.append(current);

        for (const auto& block : blocks) {
            if (!block.contains("Audio/Source")) continue;
            if (block.contains("Video/Source") || block.contains("v4l2") || block.contains("libcamera")) continue;

            uint32_t nodeId = 0;
            QString name = "Unknown";
            QString desc = "";

            for (const auto& line : block.split('\n')) {
                QString trimmed = line.trimmed();

                // id line: "id 42, type PipeWire:..."
                if (trimmed.startsWith("id ")) {
                    auto parts = trimmed.split(',');
                    if (!parts.isEmpty()) {
                        bool ok = false;
                        uint32_t id = parts[0].mid(3).trimmed().toUInt(&ok);
                        if (ok) nodeId = id;
                    }
                }

                // Properties
                auto extractProp = [&](const QString& key) -> QString {
                    if (!trimmed.startsWith(key)) return {};
                    int eq = trimmed.indexOf('=');
                    if (eq < 0) return {};
                    QString val = trimmed.mid(eq + 1).trimmed();
                    if (val.startsWith('"') && val.endsWith('"'))
                        val = val.mid(1, val.size() - 2);
                    return val;
                };

                auto v = extractProp("node.description");
                if (!v.isEmpty()) desc = v;
                v = extractProp("node.name");
                if (!v.isEmpty()) name = v;
            }

            if (nodeId == 0) continue;

            QVariantMap src;
            src["nodeId"] = nodeId;
            src["name"] = name;
            src["description"] = desc.isEmpty() ? name : desc;
            m_sources.append(src);
        }

        emit sourcesChanged();
        setStatus(QString("Found %1 audio source(s)").arg(m_sources.size()));
    }

    Q_INVOKABLE void startCapture() {
        if (m_capturing) return;

        uint32_t targetNode = PW_ID_ANY;
        if (m_currentSourceIndex >= 0 && m_currentSourceIndex < m_sources.size()) {
            auto src = m_sources[m_currentSourceIndex].toMap();
            targetNode = src["nodeId"].toUInt();
        }

        m_loop = pw_thread_loop_new("mic-test", nullptr);
        pw_thread_loop_lock(m_loop);

        static const pw_stream_events events = [] {
            pw_stream_events e{};
            e.version = PW_VERSION_STREAM_EVENTS;
            e.process = &MicTest::onProcess;
            return e;
        }();

        auto* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE,     "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE,     "Communication",
            PW_KEY_APP_NAME,       "mic-test",
            PW_KEY_NODE_NAME,      "mic-test-capture",
            nullptr);

        m_stream = pw_stream_new_simple(
            pw_thread_loop_get_loop(m_loop),
            "mic-test-stream",
            props, &events, this);

        spa_audio_info_raw info{};
        info.format   = SPA_AUDIO_FORMAT_S16_LE;
        info.rate     = 16000;
        info.channels = 1;
        info.position[0] = SPA_AUDIO_CHANNEL_MONO;

        uint8_t buf[1024];
        spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
        const spa_pod* params[1];
        params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

        pw_stream_connect(m_stream, PW_DIRECTION_INPUT, targetNode,
            static_cast<pw_stream_flags>(
                PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
            params, 1);

        pw_thread_loop_unlock(m_loop);
        pw_thread_loop_start(m_loop);

        m_capturing = true;
        emit capturingChanged();
        setStatus(targetNode == PW_ID_ANY
            ? "Capturing from default source"
            : QString("Capturing from node %1").arg(targetNode));
    }

    Q_INVOKABLE void stopCapture() {
        if (!m_capturing) return;
        if (m_loop) {
            pw_thread_loop_stop(m_loop);
            if (m_stream) { pw_stream_destroy(m_stream); m_stream = nullptr; }
            pw_thread_loop_destroy(m_loop);
            m_loop = nullptr;
        }
        m_capturing = false;
        m_peak.store(0.0f, std::memory_order_relaxed);
        m_rms.store(0.0f, std::memory_order_relaxed);
        emit capturingChanged();
        setStatus("Capture stopped");
    }

    Q_INVOKABLE void startRecording() {
        if (m_recording) return;
        std::lock_guard lk(m_recMtx);
        m_recBuf.clear();
        // 3 seconds at 16kHz mono S16_LE = 96000 samples = 192000 bytes
        m_recBuf.reserve(192000);
        m_recRemain = 16000 * 3;  // samples
        m_recording = true;
        emit recordingChanged();
        setStatus("Recording 3s...");
    }

    Q_INVOKABLE void playRecording() {
        if (m_playing) return;
        if (!QFile::exists("/tmp/pw-mic-test.wav")) {
            setStatus("No recording found at /tmp/pw-mic-test.wav");
            return;
        }
        m_playing = true;
        emit playingChanged();
        setStatus("Playing...");

        auto* proc = new QProcess(this);
        connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int code, QProcess::ExitStatus) {
                m_playing = false;
                emit playingChanged();
                setStatus(code == 0 ? "Playback finished" : "Playback failed");
                proc->deleteLater();
            });
        proc->start("pw-play", {"/tmp/pw-mic-test.wav"});
    }

private:
    void setStatus(const QString& s) {
        m_statusText = s;
        emit statusTextChanged();
    }

    // PipeWire RT callback -- runs on pw_thread_loop's thread
    static void onProcess(void* userdata) {
        auto* self = static_cast<MicTest*>(userdata);

        pw_buffer* b = pw_stream_dequeue_buffer(self->m_stream);
        if (!b) return;

        spa_buffer* sb = b->buffer;
        if (sb->n_datas == 0 || !sb->datas[0].data) {
            pw_stream_queue_buffer(self->m_stream, b);
            return;
        }

        auto* src = static_cast<const int16_t*>(sb->datas[0].data);
        uint32_t bytes = sb->datas[0].chunk->size;
        uint32_t nSamples = bytes / sizeof(int16_t);

        if (nSamples == 0) {
            pw_stream_queue_buffer(self->m_stream, b);
            return;
        }

        // Compute peak and RMS
        int32_t peakAbs = 0;
        int64_t sumSq = 0;
        for (uint32_t i = 0; i < nSamples; ++i) {
            int32_t s = src[i];
            int32_t a = s < 0 ? -s : s;
            if (a > peakAbs) peakAbs = a;
            sumSq += static_cast<int64_t>(s) * s;
        }
        float peakF = static_cast<float>(peakAbs) / 32768.0f;
        float rmsF = std::sqrt(static_cast<float>(sumSq) / static_cast<float>(nSamples)) / 32768.0f;

        // Smooth: fast attack, slow decay
        float prevPeak = self->m_peak.load(std::memory_order_relaxed);
        float prevRms = self->m_rms.load(std::memory_order_relaxed);
        float newPeak = peakF > prevPeak ? peakF : prevPeak * 0.92f;
        float newRms = rmsF > prevRms ? rmsF : prevRms * 0.92f;
        self->m_peak.store(newPeak, std::memory_order_relaxed);
        self->m_rms.store(newRms, std::memory_order_relaxed);

        // Recording (check both flags: m_recording is set by UI thread,
        // m_recJustFinished guards the window between RT signaling done
        // and UI thread clearing m_recording)
        if (self->m_recording && !self->m_recJustFinished.load(std::memory_order_relaxed)) {
            std::lock_guard lk(self->m_recMtx);
            uint32_t toStore = std::min(nSamples, static_cast<uint32_t>(self->m_recRemain));
            if (toStore > 0) {
                auto* raw = reinterpret_cast<const uint8_t*>(src);
                self->m_recBuf.insert(self->m_recBuf.end(), raw, raw + toStore * 2);
                self->m_recRemain -= toStore;
            }
            if (self->m_recRemain <= 0) {
                // Write WAV file (from RT thread -- /tmp is fast, acceptable)
                self->writeWav();
                // Signal UI thread to update m_recording and emit signal
                self->m_recJustFinished.store(true, std::memory_order_release);
            }
        }

        pw_stream_queue_buffer(self->m_stream, b);
    }

    void writeWav() {
        // Called from RT thread with m_recMtx held
        WavHeader hdr;
        hdr.dataSize = static_cast<uint32_t>(m_recBuf.size());
        hdr.chunkSize = 36 + hdr.dataSize;

        std::ofstream f("/tmp/pw-mic-test.wav", std::ios::binary | std::ios::trunc);
        if (!f) return;
        f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
        f.write(reinterpret_cast<const char*>(m_recBuf.data()), m_recBuf.size());
    }

    // --- Members ---
    QTimer* m_uiTimer = nullptr;
    QVariantList m_sources;
    int m_currentSourceIndex = -1;

    // PipeWire
    pw_thread_loop* m_loop = nullptr;
    pw_stream* m_stream = nullptr;

    // Levels (written from RT thread, read from UI timer)
    std::atomic<float> m_peak{0.0f};
    std::atomic<float> m_rms{0.0f};

    // Recording
    std::mutex m_recMtx;
    std::vector<uint8_t> m_recBuf;
    int m_recRemain = 0;
    bool m_recording = false;
    bool m_capturing = false;
    bool m_playing = false;
    std::atomic<bool> m_recJustFinished{false};

    QString m_statusText = "Ready";

signals:
    void sourcesChanged();
    void currentSourceIndexChanged();
    void levelsChanged();
    void capturingChanged();
    void recordingChanged();
    void playingChanged();
    void statusTextChanged();
};

// ---------------------------------------------------------------------------
// QML UI as string literal
// ---------------------------------------------------------------------------
static const char* QML_MAIN = R"QML(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    visible: true
    width: 500
    height: 460
    title: "PipeWire Mic Test"
    color: "#1e1e2e"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // --- Source selector ---
        Label {
            text: "Audio Source"
            color: "#cdd6f4"
            font.pixelSize: 14
            font.bold: true
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true

            ComboBox {
                id: sourceCombo
                Layout.fillWidth: true
                model: micTest.sources
                textRole: "description"
                currentIndex: micTest.currentSourceIndex
                onCurrentIndexChanged: micTest.currentSourceIndex = currentIndex

                delegate: ItemDelegate {
                    width: sourceCombo.width
                    contentItem: Text {
                        text: modelData.description + " (id:" + modelData.nodeId + ")"
                        color: "#cdd6f4"
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    background: Rectangle {
                        color: highlighted ? "#45475a" : "#313244"
                    }
                }
                background: Rectangle {
                    color: "#313244"
                    border.color: "#585b70"
                    border.width: 1
                    radius: 4
                }
                contentItem: Text {
                    text: sourceCombo.displayText
                    color: "#cdd6f4"
                    font.pixelSize: 13
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 8
                    elide: Text.ElideRight
                }
            }

            Button {
                text: "Refresh"
                onClicked: micTest.refreshSources()
                background: Rectangle { color: "#585b70"; radius: 4 }
                contentItem: Text { text: parent.text; color: "#cdd6f4"; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter }
            }
        }

        // --- Capture controls ---
        RowLayout {
            spacing: 8
            Button {
                text: micTest.capturing ? "Stop Capture" : "Start Capture"
                onClicked: micTest.capturing ? micTest.stopCapture() : micTest.startCapture()
                background: Rectangle {
                    color: micTest.capturing ? "#f38ba8" : "#a6e3a1"
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text; color: "#1e1e2e"; font.pixelSize: 13; font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // --- VU Meter ---
        Label {
            text: "Level"
            color: "#cdd6f4"
            font.pixelSize: 14
            font.bold: true
        }

        // Peak bar
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true

            Label { text: "Peak"; color: "#a6adc8"; font.pixelSize: 12 }
            Rectangle {
                Layout.fillWidth: true
                height: 28
                color: "#313244"
                radius: 4

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * Math.min(micTest.peak, 1.0)
                    radius: 4
                    color: micTest.peak > 0.9 ? "#f38ba8"
                         : micTest.peak > 0.6 ? "#fab387"
                         : "#a6e3a1"

                    Behavior on width { NumberAnimation { duration: 30 } }
                }

                Text {
                    anchors.centerIn: parent
                    text: micTest.peakPercent.toFixed(1) + "%"
                    color: "#cdd6f4"
                    font.pixelSize: 13
                    font.bold: true
                }
            }

            Label { text: "RMS"; color: "#a6adc8"; font.pixelSize: 12 }
            Rectangle {
                Layout.fillWidth: true
                height: 28
                color: "#313244"
                radius: 4

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * Math.min(micTest.rms, 1.0)
                    radius: 4
                    color: "#89b4fa"

                    Behavior on width { NumberAnimation { duration: 30 } }
                }

                Text {
                    anchors.centerIn: parent
                    text: micTest.rmsPercent.toFixed(1) + "%"
                    color: "#cdd6f4"
                    font.pixelSize: 13
                    font.bold: true
                }
            }
        }

        // --- Record / Play ---
        RowLayout {
            spacing: 8

            Button {
                text: micTest.recording ? "Recording..." : "Record 3s"
                enabled: micTest.capturing && !micTest.recording
                onClicked: micTest.startRecording()
                background: Rectangle {
                    color: micTest.recording ? "#f38ba8" : (parent.enabled ? "#f5c2e7" : "#45475a")
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text; color: "#1e1e2e"; font.pixelSize: 13; font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Button {
                text: micTest.playing ? "Playing..." : "Play"
                enabled: !micTest.playing
                onClicked: micTest.playRecording()
                background: Rectangle {
                    color: parent.enabled ? "#89dceb" : "#45475a"
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text; color: "#1e1e2e"; font.pixelSize: 13; font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        // --- Status ---
        Rectangle {
            Layout.fillWidth: true
            height: 32
            color: "#313244"
            radius: 4

            Text {
                anchors.fill: parent
                anchors.leftMargin: 8
                text: micTest.statusText
                color: "#bac2de"
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }

        Item { Layout.fillHeight: true }
    }

    Component.onCompleted: {
        micTest.refreshSources()
    }
}
)QML";

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    pw_init(&argc, &argv);

    QGuiApplication app(argc, argv);
    app.setApplicationName("mic-test");

    MicTest micTest;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("micTest", &micTest);

    engine.loadData(QML_MAIN, QUrl("qrc:/main.qml"));
    if (engine.rootObjects().isEmpty()) {
        qCritical("Failed to load QML");
        return 1;
    }

    int ret = app.exec();
    pw_deinit();
    return ret;
}

#include "main.moc"
