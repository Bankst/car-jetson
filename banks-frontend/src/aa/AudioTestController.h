#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

class AudioTestController : public QObject {
    Q_OBJECT
    Q_PROPERTY(float micLevel READ micLevel NOTIFY micLevelChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString pipewireInfo READ pipewireInfo CONSTANT)

public:
    explicit AudioTestController(QObject* parent = nullptr);

    float micLevel() const { return m_micLevel; }
    QString statusText() const { return m_statusText; }
    QString pipewireInfo() const;

    Q_INVOKABLE void setMasterVolume(float vol);
    Q_INVOKABLE void setMicGain(float gain);
    Q_INVOKABLE void testSpeakers();
    Q_INVOKABLE void testMicLoopback();
    Q_INVOKABLE void recordMic(int seconds);
    Q_INVOKABLE void playRecording();
    Q_INVOKABLE float channelLevel(int ch);

signals:
    void micLevelChanged();
    void statusTextChanged();

private:
    void pollMicLevel();
    void setStatusText(const QString& s);

    QTimer m_pollTimer;
    float m_micLevel = 0.0f;
    QString m_statusText;
};
