#pragma once

#include <QObject>
#include <QDBusUnixFileDescriptor>

class QSocketNotifier;
class QDBusInterface;

// App-wide sleep/wake coordinator.
//
// Subscribers connect to `sleeping()` (tear down) and `waking()` (restore).
// Triggers:
//   - systemd-logind PrepareForSleep D-Bus signal (real suspend)
//   - SIGUSR1 (manual sleep, no real suspend)
//   - SIGUSR2 (manual wake)
//
// On real suspend we hold a logind delay-type inhibitor lock so subscribers
// get a window (~5s default) to finish ByeBye / disk flush / etc before the
// kernel actually suspends.
class SleepManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool asleep READ asleep NOTIFY stateChanged)

public:
    explicit SleepManager(QObject* parent = nullptr);
    ~SleepManager() override;

    bool asleep() const { return m_asleep; }

    // Manual sleep/wake triggers (also exposed via SIGUSR1/SIGUSR2).
    Q_INVOKABLE void requestSleep();
    Q_INVOKABLE void requestWake();

signals:
    // Emitted on the GUI thread. Subscribers should tear down synchronously
    // (or kick off async work — the inhibitor lock holds suspend until we
    // release it, but only briefly).
    void sleeping();
    void waking();
    void stateChanged();

private slots:
    void onPrepareForSleep(bool start);

private:
    void takeInhibitor();
    void releaseInhibitor();
    void installSignalHandlers();

    QDBusInterface* m_logind = nullptr;
    QDBusUnixFileDescriptor m_inhibitorFd;
    bool m_asleep = false;

    int m_sigPipe[2] = {-1, -1};
    QSocketNotifier* m_sigNotifier = nullptr;
    static SleepManager* s_instance;
    static void posixSignalHandler(int sig);
};
