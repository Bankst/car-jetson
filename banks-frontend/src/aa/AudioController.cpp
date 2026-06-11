#include "AudioController.h"

#include <alsa/asoundlib.h>

#include <QProcess>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QHash>
#include <QDebug>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <vector>

// ---------------------------------------------------------------------------
// Constants — mirror banks-audio-hw.py / banks-audio-route.sh
// ---------------------------------------------------------------------------
namespace {
constexpr const char* CARD     = "hw:APE";
constexpr int   SR             = 48000;
constexpr int   PEQ_SHIFT      = 30;     // Q1.30
constexpr int   MAX_STAGES     = 12;     // silicon biquads per channel
constexpr int   GAIN_BLOB_LEN  = 62;     // 2 + 12*5
constexpr int   SHIFT_BLOB_LEN = 14;     // 2 + 12
constexpr double BW_Q          = 0.70710678; // Butterworth / LR4 section Q

constexpr double MVC_DB_MIN = -80.0;
constexpr double MVC_DB_SPAN = 90.0;
constexpr double LEVEL_DB_MIN = -24.0;
constexpr double LEVEL_DB_MAX = 6.0;     // Q1.30 pre_gain headroom

struct Biquad { double b0, b1, b2, a1, a2; };

Biquad rbjPeaking(double f, double q, double g) {
    double A = std::pow(10.0, g / 40.0);
    double w0 = 2*M_PI*f/SR, cw = std::cos(w0), alpha = std::sin(w0)/(2*q);
    double a0 = 1 + alpha/A;
    return {(1+alpha*A)/a0,(-2*cw)/a0,(1-alpha*A)/a0,(-2*cw)/a0,(1-alpha/A)/a0};
}
Biquad rbjLowShelf(double f, double q, double g) {
    double A = std::pow(10.0, g/40.0);
    double w0 = 2*M_PI*f/SR, cw = std::cos(w0);
    double alpha = std::sin(w0)/2 * std::sqrt((A+1/A)*(1/q-1)+2), ts = 2*std::sqrt(A);
    double a0 = (A+1)+(A-1)*cw+ts*alpha;
    return {A*((A+1)-(A-1)*cw+ts*alpha)/a0, 2*A*((A-1)-(A+1)*cw)/a0,
            A*((A+1)-(A-1)*cw-ts*alpha)/a0, -2*((A-1)+(A+1)*cw)/a0,
            ((A+1)+(A-1)*cw-ts*alpha)/a0};
}
Biquad rbjHighShelf(double f, double q, double g) {
    double A = std::pow(10.0, g/40.0);
    double w0 = 2*M_PI*f/SR, cw = std::cos(w0);
    double alpha = std::sin(w0)/2 * std::sqrt((A+1/A)*(1/q-1)+2), ts = 2*std::sqrt(A);
    double a0 = (A+1)-(A-1)*cw+ts*alpha;
    return {A*((A+1)+(A-1)*cw+ts*alpha)/a0, -2*A*((A-1)+(A+1)*cw)/a0,
            A*((A+1)+(A-1)*cw-ts*alpha)/a0, 2*((A-1)-(A+1)*cw)/a0,
            ((A+1)-(A-1)*cw+ts*alpha)/a0};
}
Biquad rbjHighpass(double f, double q) {
    double w0 = 2*M_PI*f/SR, cw = std::cos(w0), alpha = std::sin(w0)/(2*q);
    double a0 = 1+alpha;
    return {((1+cw)/2)/a0, (-(1+cw))/a0, ((1+cw)/2)/a0, (-2*cw)/a0, (1-alpha)/a0};
}
Biquad rbjLowpass(double f, double q) {
    double w0 = 2*M_PI*f/SR, cw = std::cos(w0), alpha = std::sin(w0)/(2*q);
    double a0 = 1+alpha;
    return {((1-cw)/2)/a0, (1-cw)/a0, ((1-cw)/2)/a0, (-2*cw)/a0, (1-alpha)/a0};
}
const Biquad kIdentity{1.0, 0.0, 0.0, 0.0, 0.0};

long quantizeQ30(double x) {
    double s = std::round(x * double(1 << PEQ_SHIFT));
    if (s >  2147483647.0) return  2147483647L;
    if (s < -2147483648.0) return -2147483648L;
    return long(s);
}

// ---- band-map presets (config-time) ----
QVector<EqBand> bandMapByName(const QString& name) {
    auto P = [](double f, double q){ return EqBand{f, "peaking", q}; };
    auto LS = [](double f){ return EqBand{f, "lowshelf", BW_Q}; };
    auto HS = [](double f){ return EqBand{f, "highshelf", BW_Q}; };
    if (name == "cabin11")
        return {LS(40),P(80,1),P(160,1),P(315,1),P(630,1),P(1250,1),P(2500,1),P(5000,1),P(8000,1),P(12500,1),HS(16000)};
    if (name == "graphic-iso-10")
        return {LS(31.5),P(63,1.4),P(125,1.4),P(250,1.4),P(500,1.4),P(1000,1.4),P(2000,1.4),P(4000,1.4),P(8000,1.4),HS(16000)};
    if (name == "graphic-iso-11")
        return {LS(31.5),P(63,1.4),P(125,1.4),P(250,1.4),P(500,1.4),P(1000,1.4),P(2000,1.4),P(4000,1.4),P(8000,1.4),P(16000,1.4),HS(20000)};
    // default: cabin10
    return {LS(40),P(80,1),P(160,1),P(315,1),P(630,1),P(1250,1),P(2500,1),P(5000,1),P(10000,1),HS(16000)};
}

QString fmtFreq(double hz) {
    if (hz >= 1000.0) return QString::number(hz/1000.0, 'g', 3) + QStringLiteral("k");
    return QString::number(hz, 'g', 3);
}

// --------------------------- libasound ctl layer ---------------------------
bool ctlExists(const QString& name) {
    snd_ctl_t* ctl = nullptr;
    if (snd_ctl_open(&ctl, CARD, 0) < 0) return false;
    snd_ctl_elem_id_t* id; snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name.toUtf8().constData());
    snd_ctl_elem_info_t* info; snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_info_set_id(info, id);
    bool ok = snd_ctl_elem_info(ctl, info) >= 0;
    snd_ctl_close(ctl);
    return ok;
}
bool writeIntArray(const QString& name, const QVector<long>& values) {
    snd_ctl_t* ctl = nullptr;
    if (snd_ctl_open(&ctl, CARD, 0) < 0) return false;
    snd_ctl_elem_id_t* id; snd_ctl_elem_id_alloca(&id);
    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_id_set_name(id, name.toUtf8().constData());
    snd_ctl_elem_value_t* val; snd_ctl_elem_value_alloca(&val);
    snd_ctl_elem_value_set_id(val, id);
    for (int i = 0; i < values.size(); ++i)
        snd_ctl_elem_value_set_integer(val, i, values[i]);
    int rc = snd_ctl_elem_write(ctl, val);
    snd_ctl_close(ctl);
    return rc >= 0;
}
bool writeIntScalar(const QString& name, long v) { return writeIntArray(name, QVector<long>{v}); }
} // namespace

// ---------------------------------------------------------------------------
AudioController::AudioController(QObject* parent) : QObject(parent) {
    loadBandMap();
    for (int i = 0; i < 8; ++i) {
        auto* sc = new SpeakerChannel(i, this);
        sc->m_eq = QVector<float>(m_bands.size(), 0.0f);
        m_speakers.append(sc);
        m_speakerObjs.append(sc);
    }
    initDefaults();

    m_hwReady = ctlExists(QStringLiteral("OPE1 PEQ Active"));
    m_codecPresent = ctlExists(QStringLiteral("CS42448 DAC1 Playback Volume"));

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(800);
    connect(&m_saveTimer, &QTimer::timeout, this, &AudioController::saveState);

    bool restored = loadState();
    recomputeActive();

    if (m_hwReady) {
        ensureKeepalive();
        writeIntScalar(QStringLiteral("OPE1 PEQ Biquad Stages"), MAX_STAGES - 1);
        applyMaster();
        applyAllChannels();
        writeIntScalar(QStringLiteral("OPE1 PEQ Active"), m_peqActive ? 1 : 0);
        setStatus(restored ? QStringLiteral("Restored saved tuning")
                           : QStringLiteral("HW ready — band map: %1").arg(m_bandMapName));
    } else {
        setStatus(QStringLiteral("APE / OPE1 PEQ not found — UI live, writes deferred"));
    }
}

AudioController::~AudioController() {
    saveState();
    if (m_keepalive) {
        m_keepalive->terminate();
        if (!m_keepalive->waitForFinished(800)) m_keepalive->kill();
        delete m_keepalive; m_keepalive = nullptr;
    }
}

void AudioController::initDefaults() {
    struct Def { const char* name; const char* role; bool custom; };
    const Def defs[8] = {
        {"Front L",  "Full-range", false}, {"Front R",  "Full-range", false},
        {"Rear L",   "Full-range", false}, {"Rear R",   "Full-range", false},
        {"Center",   "Full-range", false}, {"Subwoofer","Subwoofer",  false},
        {"Custom L", "Tweeter",    true},  {"Custom R", "Tweeter",    true},
    };
    for (int i = 0; i < 8; ++i) {
        m_speakers[i]->m_name = QString::fromUtf8(defs[i].name);
        m_speakers[i]->m_role = QString::fromUtf8(defs[i].role);
        m_speakers[i]->m_customizable = defs[i].custom;
        applyRoleDefaults(m_speakers[i]);
    }
}

void AudioController::applyRoleDefaults(SpeakerChannel* s) {
    const QString& r = s->m_role;
    // sane starting xover per role; user tweaks live afterwards
    s->m_hpOn = false; s->m_lpOn = false;
    if (r == "Subwoofer")        { s->m_lpOn = true;  s->m_lpFreq = 80;   s->m_lpStages = 2; }
    else if (r == "Woofer")      { s->m_lpOn = true;  s->m_lpFreq = 1500; s->m_lpStages = 2; }
    else if (r == "Midrange")    { s->m_hpOn = true;  s->m_hpFreq = 80;   s->m_hpStages = 2;
                                   s->m_lpOn = true;  s->m_lpFreq = 4000; s->m_lpStages = 2; }
    else if (r == "Tweeter")     { s->m_hpOn = true;  s->m_hpFreq = 3500; s->m_hpStages = 2; }
    else if (r == "Side surround"){ s->m_hpOn = true; s->m_hpFreq = 100;  s->m_hpStages = 1; }
    else if (r == "Rear fill")   { s->m_hpOn = true;  s->m_hpFreq = 80;   s->m_hpStages = 1; }
    // Full-range: no xover
}

SpeakerChannel* AudioController::chan(int slot) const {
    return (slot >= 0 && slot < m_speakers.size()) ? m_speakers[slot] : nullptr;
}

qreal AudioController::masterVolumeDb() const { return MVC_DB_MIN + m_masterVol * MVC_DB_SPAN; }
QStringList AudioController::speakerModes() const { return {"2.0","2.1","4.0","4.1","5.1","7.1"}; }
QStringList AudioController::presets() const { return {"flat","bass+","voice","treble+","v-shape"}; }
QStringList AudioController::roles() const {
    return {"Full-range","Subwoofer","Woofer","Midrange","Tweeter","Side surround","Rear fill"};
}
QStringList AudioController::eqBandLabels() const {
    QStringList l; for (const EqBand& b : m_bands) l << fmtFreq(b.freq); return l;
}

void AudioController::applyMaster() {
    if (!m_hwReady) return;
    long mvc = std::lround(masterVolumeDb() * 100.0 + 12000.0);
    mvc = std::max(0L, std::min(16000L, mvc));
    writeIntScalar(QStringLiteral("MVC1 Volume"), mvc);
    writeIntScalar(QStringLiteral("MVC1 Mute"), m_masterMute ? 1 : 0);
}

// Build the 62-int Q1.30 gain blob: pre_gain (level/mute), then the channel's
// biquad chain [HP][LP][EQ] capped at 12, identity-padded, post_gain unity.
static QVector<long> buildGainBlob(const SpeakerChannel* s, const QVector<EqBand>& bands) {
    std::vector<Biquad> chain;
    if (s->hpStagesActive() > 0) {
        Biquad hp = rbjHighpass(s->hpFreq(), BW_Q);
        for (int k = 0; k < s->hpStagesActive(); ++k) chain.push_back(hp);
    }
    if (s->lpStagesActive() > 0) {
        Biquad lp = rbjLowpass(s->lpFreq(), BW_Q);
        for (int k = 0; k < s->lpStagesActive(); ++k) chain.push_back(lp);
    }
    QVariantList eq = s->eqGains();
    for (int i = 0; i < bands.size() && int(chain.size()) < MAX_STAGES; ++i) {
        double g = (i < eq.size()) ? eq[i].toDouble() : 0.0;
        if (std::abs(g) > 0.001) {
            const EqBand& b = bands[i];
            if (b.type == "lowshelf")       chain.push_back(rbjLowShelf(b.freq, b.q, g));
            else if (b.type == "highshelf") chain.push_back(rbjHighShelf(b.freq, b.q, g));
            else                            chain.push_back(rbjPeaking(b.freq, b.q, g));
        } else {
            chain.push_back(kIdentity);
        }
    }
    while (int(chain.size()) < MAX_STAGES) chain.push_back(kIdentity);

    double pre = s->mute() ? 0.0
        : std::pow(10.0, std::clamp(double(s->levelDb()), LEVEL_DB_MIN, LEVEL_DB_MAX) / 20.0);
    QVector<long> out; out.reserve(GAIN_BLOB_LEN);
    out.append(quantizeQ30(pre));
    for (int i = 0; i < MAX_STAGES; ++i) {
        const Biquad& bq = chain[i];
        out.append(quantizeQ30(bq.b0)); out.append(quantizeQ30(bq.b1));
        out.append(quantizeQ30(bq.b2)); out.append(quantizeQ30(bq.a1));
        out.append(quantizeQ30(bq.a2));
    }
    out.append(quantizeQ30(1.0)); // post_gain unity
    return out;
}

void AudioController::applyChannel(int slot) {
    if (!m_hwReady) return;
    SpeakerChannel* s = chan(slot); if (!s) return;
    ensureKeepalive();
    bool was = m_peqActive;
    if (was) { writeIntScalar(QStringLiteral("OPE1 PEQ Active"), 0); QThread::msleep(10); }
    writeIntArray(QStringLiteral("OPE1 PEQ Channel-%1 biquad gain params").arg(slot),
                  buildGainBlob(s, m_bands));
    writeIntArray(QStringLiteral("OPE1 PEQ Channel-%1 biquad shift params").arg(slot),
                  QVector<long>(SHIFT_BLOB_LEN, PEQ_SHIFT));
    if (was) { QThread::msleep(10); writeIntScalar(QStringLiteral("OPE1 PEQ Active"), 1); }
}

void AudioController::applyAllChannels() {
    if (!m_hwReady) return;
    ensureKeepalive();
    bool was = m_peqActive;
    if (was) { writeIntScalar(QStringLiteral("OPE1 PEQ Active"), 0); QThread::msleep(10); }
    for (int i = 0; i < m_speakers.size(); ++i) {
        writeIntArray(QStringLiteral("OPE1 PEQ Channel-%1 biquad gain params").arg(i),
                      buildGainBlob(m_speakers[i], m_bands));
        writeIntArray(QStringLiteral("OPE1 PEQ Channel-%1 biquad shift params").arg(i),
                      QVector<long>(SHIFT_BLOB_LEN, PEQ_SHIFT));
    }
    if (was) { QThread::msleep(10); writeIntScalar(QStringLiteral("OPE1 PEQ Active"), 1); }
}

void AudioController::recomputeActive() {
    static const QHash<QString, QVector<int>> modeSlots = {
        {"2.0", {0,1}}, {"2.1", {0,1,5}}, {"4.0", {0,1,2,3}}, {"4.1", {0,1,2,3,5}},
        {"5.1", {0,1,2,3,4,5}}, {"7.1", {0,1,2,3,4,5,6,7}},
    };
    QVector<int> on = modeSlots.value(m_mode, {0,1});
    for (int i = 0; i < m_speakers.size(); ++i) {
        bool a = on.contains(i);
        if (m_speakers[i]->m_active != a) { m_speakers[i]->m_active = a; emit m_speakers[i]->changed(); }
    }
}

void AudioController::ensureKeepalive() {
    if (m_keepalive) return;
    auto spawn = [](int ch) -> QProcess* {
        auto* p = new QProcess();
        p->start("aplay", {"-q","-D","hw:APE,0","-f","S16_LE",
                           "-c",QString::number(ch),"-r",QString::number(SR),"/dev/zero"});
        if (!p->waitForStarted(1500)) { delete p; return nullptr; }
        QThread::msleep(150);
        if (p->state() != QProcess::Running) { p->deleteLater(); return nullptr; }
        return p;
    };
    m_keepalive = spawn(8);
    if (!m_keepalive) m_keepalive = spawn(2);
}

// ---- master / mode ----
void AudioController::setMasterVolume(qreal v) {
    v = std::clamp<qreal>(v, 0.0, 1.0);
    if (qFuzzyCompare(v, m_masterVol)) return;
    m_masterVol = v; applyMaster(); emit masterChanged(); scheduleSave();
}
void AudioController::setMasterMute(bool m) {
    if (m == m_masterMute) return;
    m_masterMute = m; applyMaster(); emit masterChanged(); scheduleSave();
}
void AudioController::setPeqActive(bool on) {
    if (on == m_peqActive) return;
    m_peqActive = on;
    if (m_hwReady) writeIntScalar(QStringLiteral("OPE1 PEQ Active"), on ? 1 : 0);
    setStatus(on ? QStringLiteral("EQ engine on")
                 : QStringLiteral("EQ bypassed (per-speaker trim/xover/mute also bypassed)"));
    emit peqActiveChanged(); scheduleSave();
}
void AudioController::setSpeakerMode(const QString& mode) {
    if (mode == m_mode || !speakerModes().contains(mode)) return;
    m_mode = mode; recomputeActive();
    if (!QStandardPaths::findExecutable("banks-audio-mode").isEmpty())
        QProcess::startDetached("banks-audio-mode", {mode});
    else
        setStatus(QStringLiteral("Mode %1 set (matrix manager absent — UI only)").arg(mode));
    emit speakerModeChanged(); scheduleSave();
}

// ---- per-speaker basics ----
void AudioController::setLevel(int slot, qreal db) {
    SpeakerChannel* s = chan(slot); if (!s) return;
    db = std::clamp<qreal>(db, LEVEL_DB_MIN, LEVEL_DB_MAX);
    if (qFuzzyCompare(db, s->m_levelDb)) return;
    s->m_levelDb = db; applyChannel(slot); emit s->changed(); scheduleSave();
}
void AudioController::setMute(int slot, bool mute) {
    SpeakerChannel* s = chan(slot); if (!s || s->m_mute == mute) return;
    s->m_mute = mute; applyChannel(slot); emit s->changed(); scheduleSave();
}
void AudioController::setName(int slot, const QString& name) {
    SpeakerChannel* s = chan(slot); if (!s || s->m_name == name) return;
    s->m_name = name; emit s->changed(); scheduleSave();
}
void AudioController::setRole(int slot, const QString& role) {
    SpeakerChannel* s = chan(slot); if (!s || s->m_role == role || !roles().contains(role)) return;
    s->m_role = role; applyRoleDefaults(s); applyChannel(slot); emit s->changed(); scheduleSave();
}
void AudioController::setBand(int slot, int band, qreal db) {
    SpeakerChannel* s = chan(slot); if (!s || band < 0 || band >= s->m_eq.size()) return;
    db = std::clamp<qreal>(db, -24.0, 24.0);
    if (qFuzzyCompare(double(s->m_eq[band]), db)) return;
    s->m_eq[band] = float(db); applyChannel(slot); emit s->changed(); scheduleSave();
}
void AudioController::applyPreset(int slot, const QString& preset) {
    SpeakerChannel* s = chan(slot); if (!s) return;
    static const QHash<QString, QVector<float>> P = {
        {"flat",    {0,0,0,0,0,0,0,0,0,0,0}},
        {"bass+",   {6,4,2,0,0,0,0,0,0,0,0}},
        {"voice",   {-3,-2,1,3,4,2,0,-2,0,0,0}},
        {"treble+", {0,0,0,0,1,3,5,6,4,3,0}},
        {"v-shape", {5,3,0,-3,-3,0,3,5,4,3,0}},
    };
    if (!P.contains(preset)) return;
    QVector<float> v = P.value(preset);
    for (int i = 0; i < s->m_eq.size(); ++i) s->m_eq[i] = (i < v.size()) ? v[i] : 0.0f;
    applyChannel(slot); emit s->changed(); scheduleSave();
    setStatus(QStringLiteral("Preset '%1' → %2").arg(preset, s->m_name));
}
void AudioController::resetSpeaker(int slot) {
    SpeakerChannel* s = chan(slot); if (!s) return;
    s->m_eq.fill(0.0f); s->m_levelDb = 0.0; s->m_mute = false;
    applyChannel(slot); emit s->changed(); scheduleSave();
}

// ---- per-speaker crossover ----
void AudioController::setHp(int slot, bool on, qreal freq, int stages) {
    SpeakerChannel* s = chan(slot); if (!s) return;
    s->m_hpOn = on;
    s->m_hpFreq = std::clamp<qreal>(freq, 20.0, 1000.0);
    s->m_hpStages = std::clamp(stages, 1, 2);
    applyChannel(slot); emit s->changed(); scheduleSave();
}
void AudioController::setLp(int slot, bool on, qreal freq, int stages) {
    SpeakerChannel* s = chan(slot); if (!s) return;
    s->m_lpOn = on;
    s->m_lpFreq = std::clamp<qreal>(freq, 40.0, 20000.0);
    s->m_lpStages = std::clamp(stages, 1, 2);
    applyChannel(slot); emit s->changed(); scheduleSave();
}
void AudioController::setAmpLowcut(int slot, bool on, qreal freq) {
    SpeakerChannel* s = chan(slot); if (!s) return;
    s->m_ampLowcutOn = on;
    s->m_ampLowcutFreq = std::clamp<qreal>(freq, 20.0, 1000.0);
    // amp owns the low-cut → OPE HP suppressed in the chain (hpStagesActive()==0)
    applyChannel(slot); emit s->changed(); scheduleSave();
}

void AudioController::testSpeaker(int slot) {
    SpeakerChannel* s = chan(slot); if (!s) return;
    QString path = QStringLiteral("/tmp/banks-spk-test-%1.wav").arg(slot);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        const int ch = 8, frames = SR; const int dataSize = frames*ch*2;
        auto u32 = [&](uint32_t v){ f.write(reinterpret_cast<const char*>(&v),4); };
        auto u16 = [&](uint16_t v){ f.write(reinterpret_cast<const char*>(&v),2); };
        f.write("RIFF",4); u32(36+dataSize); f.write("WAVE",4);
        f.write("fmt ",4); u32(16); u16(1); u16(ch); u32(SR);
        u32(SR*ch*2); u16(ch*2); u16(16); f.write("data",4); u32(dataSize);
        const int fade = SR/100;
        for (int i = 0; i < frames; ++i) {
            double env = 0.5*std::sin(2*M_PI*440.0*i/SR);
            if (i < fade) env *= double(i)/fade;
            else if (i > frames-fade) env *= double(frames-i)/fade;
            int16_t v = int16_t(env*32767.0);
            for (int c = 0; c < ch; ++c) { int16_t o = (c==slot)?v:0; f.write(reinterpret_cast<const char*>(&o),2); }
        }
        f.close();
        QProcess::startDetached("pw-play", {"--target=@DEFAULT_AUDIO_SINK@", path});
        setStatus(QStringLiteral("Test tone → %1").arg(s->m_name));
    }
}

void AudioController::save() { saveState(); }
void AudioController::scheduleSave() { m_saveTimer.start(); }

// ---- config-time band map ----
void AudioController::loadBandMap() {
    const QStringList paths = {
        QStringLiteral("/data/banks-frontend/eq-profile.json"),
        QStringLiteral("/etc/banks-audio/eq-profile.json"),
    };
    for (const QString& p : paths) {
        QFile f(p);
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (o.contains("bands") && o.value("bands").isArray()) {
            QVector<EqBand> bm;
            for (const QJsonValue& v : o.value("bands").toArray()) {
                QJsonObject b = v.toObject();
                bm.append({ b.value("freq").toDouble(1000),
                            b.value("type").toString("peaking"),
                            b.value("q").toDouble(1.0) });
            }
            if (!bm.isEmpty()) { m_bands = bm; m_bandMapName = QStringLiteral("custom"); return; }
        }
        QString name = o.value("bandMap").toString();
        if (!name.isEmpty()) { m_bandMapName = name; m_bands = bandMapByName(name); return; }
    }
    m_bandMapName = QStringLiteral("cabin10");
    m_bands = bandMapByName(m_bandMapName);
}

// ---- live state persistence ----
QString AudioController::stateFilePath() const {
    QString dir = QStringLiteral("/data/banks-frontend");
    QFileInfo fi(QStringLiteral("/data"));
    if (!(fi.exists() && fi.isWritable()))
        dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/audio.json");
}

bool AudioController::loadState() {
    QFile f(stateFilePath());
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (root.isEmpty()) return false;
    m_masterVol  = root.value("masterVolume").toDouble(m_masterVol);
    m_masterMute = root.value("masterMute").toBool(false);
    m_peqActive  = root.value("peqActive").toBool(true);
    m_mode       = root.value("mode").toString(m_mode);
    QJsonArray chans = root.value("channels").toArray();
    for (int i = 0; i < chans.size() && i < m_speakers.size(); ++i) {
        QJsonObject c = chans[i].toObject();
        SpeakerChannel* s = m_speakers[i];
        s->m_name    = c.value("name").toString(s->m_name);
        s->m_role    = c.value("role").toString(s->m_role);
        s->m_levelDb = c.value("levelDb").toDouble(0.0);
        s->m_mute    = c.value("mute").toBool(false);
        s->m_hpOn    = c.value("hpOn").toBool(s->m_hpOn);
        s->m_hpFreq  = c.value("hpFreq").toDouble(s->m_hpFreq);
        s->m_hpStages= c.value("hpStages").toInt(s->m_hpStages);
        s->m_lpOn    = c.value("lpOn").toBool(s->m_lpOn);
        s->m_lpFreq  = c.value("lpFreq").toDouble(s->m_lpFreq);
        s->m_lpStages= c.value("lpStages").toInt(s->m_lpStages);
        s->m_ampLowcutOn   = c.value("ampLowcutOn").toBool(false);
        s->m_ampLowcutFreq = c.value("ampLowcutFreq").toDouble(s->m_ampLowcutFreq);
        QJsonArray eq = c.value("eq").toArray();
        for (int b = 0; b < eq.size() && b < s->m_eq.size(); ++b) s->m_eq[b] = float(eq[b].toDouble());
        emit s->changed();
    }
    emit masterChanged(); emit peqActiveChanged(); emit speakerModeChanged();
    return true;
}

void AudioController::saveState() {
    QJsonObject root;
    root["masterVolume"] = m_masterVol; root["masterMute"] = m_masterMute;
    root["peqActive"] = m_peqActive; root["mode"] = m_mode;
    root["bandMap"] = m_bandMapName;
    QJsonArray chans;
    for (SpeakerChannel* s : m_speakers) {
        QJsonObject c;
        c["name"]=s->m_name; c["role"]=s->m_role; c["levelDb"]=s->m_levelDb; c["mute"]=s->m_mute;
        c["hpOn"]=s->m_hpOn; c["hpFreq"]=s->m_hpFreq; c["hpStages"]=s->m_hpStages;
        c["lpOn"]=s->m_lpOn; c["lpFreq"]=s->m_lpFreq; c["lpStages"]=s->m_lpStages;
        c["ampLowcutOn"]=s->m_ampLowcutOn; c["ampLowcutFreq"]=s->m_ampLowcutFreq;
        QJsonArray eq; for (float g : s->m_eq) eq.append(g);
        c["eq"]=eq;
        chans.append(c);
    }
    root["channels"] = chans;
    QFile f(stateFilePath());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void AudioController::setStatus(const QString& s) { m_statusText = s; emit statusTextChanged(); }
