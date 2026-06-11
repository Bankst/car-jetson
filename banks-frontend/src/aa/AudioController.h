#pragma once

// AudioController — cabin audio manager for the CS42448 8-out TDM path.
//
// Drives the Tegra AHUB HW DSP on silicon:
//   ADMAIF1 -> MVC1 (master vol) -> OPE1 (8-ch PEQ) -> I2S5 (8-slot TDM) -> CS42448
//
// Each OPE PEQ channel has a 12-biquad budget. We spend it as:
//   [ HP xover ] + [ LP xover ] + [ EQ bands ]   (<= 12 stages)
//   - HP/LP: 1 stage = 12 dB/oct (Butterworth), 2 stages = 24 dB/oct (LR4).
//   - pre_gain (blob index 0) = per-speaker level trim / mute.
//   - remaining stages = parametric EQ bands from the active band map.
//
// Band map (count 10/11, cabin vs graphic) is CONFIG-TIME: loaded from
// /etc/banks-audio/eq-profile.json (or /data override) at startup. Per-speaker
// role + crossover + level + mute + EQ gains are LIVE and persist to
// /data/banks-frontend/audio.json.
//
// All control is ALSA kcontrol writes on card "APE" via libasound (ported from
// banks-audio-hw.py). CS42448 codec controls are used when present and degrade
// gracefully when the codec hasn't enumerated.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <QTimer>
#include <QList>

class AudioController;
class QProcess;

// EQ band definition (config-time: freq + filter type + Q).
struct EqBand {
    double freq;
    QString type;   // "peaking" | "lowshelf" | "highshelf"
    double q;
};

// One physical DAC channel / speaker. Read from QML; mutated via AudioController.
class SpeakerChannel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int slot READ slot CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString role READ role NOTIFY changed)
    Q_PROPERTY(bool customizable READ customizable CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool mute READ mute NOTIFY changed)
    Q_PROPERTY(qreal levelDb READ levelDb NOTIFY changed)
    Q_PROPERTY(QVariantList eqGains READ eqGains NOTIFY changed)
    // Crossover (live)
    Q_PROPERTY(bool hpOn READ hpOn NOTIFY changed)
    Q_PROPERTY(qreal hpFreq READ hpFreq NOTIFY changed)
    Q_PROPERTY(int hpStages READ hpStages NOTIFY changed)        // 1 = 12 dB/oct, 2 = 24
    Q_PROPERTY(bool lpOn READ lpOn NOTIFY changed)
    Q_PROPERTY(qreal lpFreq READ lpFreq NOTIFY changed)
    Q_PROPERTY(int lpStages READ lpStages NOTIFY changed)
    // External amp low-cut (informational; forces OPE HP off)
    Q_PROPERTY(bool ampLowcutOn READ ampLowcutOn NOTIFY changed)
    Q_PROPERTY(qreal ampLowcutFreq READ ampLowcutFreq NOTIFY changed)
    // Stage budget
    Q_PROPERTY(int stagesUsed READ stagesUsed NOTIFY changed)
    Q_PROPERTY(int stagesMax READ stagesMax CONSTANT)
    Q_PROPERTY(int eqBandsAvailable READ eqBandsAvailable NOTIFY changed)
public:
    explicit SpeakerChannel(int slot, QObject* parent = nullptr) : QObject(parent), m_slot(slot) {}

    int slot() const { return m_slot; }
    QString name() const { return m_name; }
    QString role() const { return m_role; }
    bool customizable() const { return m_customizable; }
    bool active() const { return m_active; }
    bool mute() const { return m_mute; }
    qreal levelDb() const { return m_levelDb; }
    QVariantList eqGains() const {
        QVariantList l;
        for (float g : m_eq) l.append(g);
        return l;
    }
    bool hpOn() const { return m_hpOn; }
    qreal hpFreq() const { return m_hpFreq; }
    int hpStages() const { return m_hpStages; }
    bool lpOn() const { return m_lpOn; }
    qreal lpFreq() const { return m_lpFreq; }
    int lpStages() const { return m_lpStages; }
    bool ampLowcutOn() const { return m_ampLowcutOn; }
    qreal ampLowcutFreq() const { return m_ampLowcutFreq; }

    // HP is suppressed when the amp does the low-cut.
    int hpStagesActive() const { return (m_hpOn && !m_ampLowcutOn) ? m_hpStages : 0; }
    int lpStagesActive() const { return m_lpOn ? m_lpStages : 0; }
    int stagesUsed() const { return hpStagesActive() + lpStagesActive(); }
    int stagesMax() const { return 12; }
    int eqBandsAvailable() const { return stagesMax() - stagesUsed(); }

signals:
    void changed();

private:
    friend class AudioController;
    int m_slot;
    QString m_name;
    QString m_role = QStringLiteral("Full-range");
    bool m_customizable = false;
    bool m_active = true;
    bool m_mute = false;
    qreal m_levelDb = 0.0;
    QVector<float> m_eq;            // sized to band-map count at construction
    bool  m_hpOn = false;
    qreal m_hpFreq = 80.0;
    int   m_hpStages = 2;
    bool  m_lpOn = false;
    qreal m_lpFreq = 3500.0;
    int   m_lpStages = 2;
    bool  m_ampLowcutOn = false;
    qreal m_ampLowcutFreq = 80.0;
};

class AudioController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<QObject*> speakers READ speakers CONSTANT)
    Q_PROPERTY(qreal masterVolume READ masterVolume NOTIFY masterChanged)
    Q_PROPERTY(qreal masterVolumeDb READ masterVolumeDb NOTIFY masterChanged)
    Q_PROPERTY(bool masterMute READ masterMute NOTIFY masterChanged)
    Q_PROPERTY(bool peqActive READ peqActive NOTIFY peqActiveChanged)
    Q_PROPERTY(QString speakerMode READ speakerMode NOTIFY speakerModeChanged)
    Q_PROPERTY(bool codecPresent READ codecPresent CONSTANT)
    Q_PROPERTY(bool hardwareReady READ hardwareReady CONSTANT)
    Q_PROPERTY(QStringList speakerModes READ speakerModes CONSTANT)
    Q_PROPERTY(QStringList presets READ presets CONSTANT)
    Q_PROPERTY(QStringList roles READ roles CONSTANT)
    Q_PROPERTY(QStringList eqBandLabels READ eqBandLabels NOTIFY bandMapChanged)
    Q_PROPERTY(int bandCount READ bandCount NOTIFY bandMapChanged)
    Q_PROPERTY(QString bandMapName READ bandMapName NOTIFY bandMapChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)

public:
    explicit AudioController(QObject* parent = nullptr);
    ~AudioController() override;

    QList<QObject*> speakers() const { return m_speakerObjs; }
    qreal masterVolume() const { return m_masterVol; }
    qreal masterVolumeDb() const;
    bool masterMute() const { return m_masterMute; }
    bool peqActive() const { return m_peqActive; }
    QString speakerMode() const { return m_mode; }
    bool codecPresent() const { return m_codecPresent; }
    bool hardwareReady() const { return m_hwReady; }
    QStringList speakerModes() const;
    QStringList presets() const;
    QStringList roles() const;
    QStringList eqBandLabels() const;
    int bandCount() const { return m_bands.size(); }
    QString bandMapName() const { return m_bandMapName; }
    QString statusText() const { return m_statusText; }

    // Master / global
    Q_INVOKABLE void setMasterVolume(qreal v01);
    Q_INVOKABLE void setMasterMute(bool m);
    Q_INVOKABLE void setPeqActive(bool on);
    Q_INVOKABLE void setSpeakerMode(const QString& mode);

    // Per-speaker — basics
    Q_INVOKABLE void setLevel(int slot, qreal db);
    Q_INVOKABLE void setMute(int slot, bool mute);
    Q_INVOKABLE void setName(int slot, const QString& name);
    Q_INVOKABLE void setRole(int slot, const QString& role);  // also loads role xover defaults
    Q_INVOKABLE void setBand(int slot, int band, qreal db);
    Q_INVOKABLE void applyPreset(int slot, const QString& preset);
    Q_INVOKABLE void resetSpeaker(int slot);
    Q_INVOKABLE void testSpeaker(int slot);

    // Per-speaker — crossover (live)
    Q_INVOKABLE void setHp(int slot, bool on, qreal freq, int stages);
    Q_INVOKABLE void setLp(int slot, bool on, qreal freq, int stages);
    Q_INVOKABLE void setAmpLowcut(int slot, bool on, qreal freq);

    Q_INVOKABLE void save();

signals:
    void masterChanged();
    void peqActiveChanged();
    void speakerModeChanged();
    void bandMapChanged();
    void statusTextChanged();

private:
    SpeakerChannel* chan(int slot) const;
    void applyChannel(int slot);
    void applyAllChannels();
    void applyMaster();
    void recomputeActive();
    void ensureKeepalive();
    void setStatus(const QString& s);
    void scheduleSave();

    void loadBandMap();                   // config-time eq-profile.json
    bool loadState();                     // live state /data audio.json
    void saveState();
    QString stateFilePath() const;
    void initDefaults();
    void applyRoleDefaults(SpeakerChannel* s);

    QVector<SpeakerChannel*> m_speakers;
    QList<QObject*> m_speakerObjs;
    QVector<EqBand> m_bands;              // active band map (config-time)
    QString m_bandMapName = QStringLiteral("cabin10");
    qreal m_masterVol = 0.7;
    bool m_masterMute = false;
    bool m_peqActive = true;
    QString m_mode = QStringLiteral("2.0");
    bool m_codecPresent = false;
    bool m_hwReady = false;
    QString m_statusText;
    QTimer m_saveTimer;
    QProcess* m_keepalive = nullptr;
};
