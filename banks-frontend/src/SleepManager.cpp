#include "SleepManager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QSocketNotifier>

#include <spdlog/spdlog.h>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

SleepManager* SleepManager::s_instance = nullptr;

SleepManager::SleepManager(QObject* parent) : QObject(parent) {
    s_instance = this;

    // logind PrepareForSleep signal — fires twice per cycle (true=sleeping,
    // false=resumed). System bus.
    m_logind = new QDBusInterface(
        QStringLiteral("org.freedesktop.login1"),
        QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"),
        QDBusConnection::systemBus(), this);

    if (m_logind->isValid()) {
        bool ok = QDBusConnection::systemBus().connect(
            QStringLiteral("org.freedesktop.login1"),
            QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"),
            QStringLiteral("PrepareForSleep"),
            this, SLOT(onPrepareForSleep(bool)));
        if (ok) {
            spdlog::info("[SleepManager] subscribed to logind PrepareForSleep");
            takeInhibitor();
        } else {
            spdlog::warn("[SleepManager] failed to subscribe to PrepareForSleep");
        }
    } else {
        spdlog::warn("[SleepManager] logind unavailable on system bus — manual triggers only");
    }

    installSignalHandlers();
}

SleepManager::~SleepManager() {
    releaseInhibitor();
    if (m_sigPipe[0] >= 0) ::close(m_sigPipe[0]);
    if (m_sigPipe[1] >= 0) ::close(m_sigPipe[1]);
    s_instance = nullptr;
}

void SleepManager::takeInhibitor() {
    if (!m_logind || !m_logind->isValid()) return;
    QDBusReply<QDBusUnixFileDescriptor> reply = m_logind->call(
        QStringLiteral("Inhibit"),
        QStringLiteral("sleep"),                 // what
        QStringLiteral("banks-frontend"),        // who
        QStringLiteral("AA session teardown, audio/BT shutdown, projectM persist"),  // why
        QStringLiteral("delay"));                // mode
    if (reply.isValid()) {
        m_inhibitorFd = reply.value();
        spdlog::info("[SleepManager] inhibitor lock acquired (fd={})",
                     m_inhibitorFd.fileDescriptor());
    } else {
        spdlog::warn("[SleepManager] Inhibit call failed: {}",
                     reply.error().message().toStdString());
    }
}

void SleepManager::releaseInhibitor() {
    if (m_inhibitorFd.isValid()) {
        spdlog::info("[SleepManager] releasing inhibitor lock");
        m_inhibitorFd = QDBusUnixFileDescriptor();  // closes the fd
    }
}

void SleepManager::onPrepareForSleep(bool start) {
    spdlog::info("[SleepManager] PrepareForSleep({})", start);
    if (start) {
        m_asleep = true;
        emit stateChanged();
        emit sleeping();
        // Release inhibitor so kernel can proceed with suspend.
        releaseInhibitor();
    } else {
        // System has resumed.
        m_asleep = false;
        emit stateChanged();
        emit waking();
        takeInhibitor();  // arm for next cycle
    }
}

void SleepManager::requestSleep() {
    if (m_asleep) return;
    spdlog::info("[SleepManager] manual sleep");
    m_asleep = true;
    emit stateChanged();
    emit sleeping();
}

void SleepManager::requestWake() {
    if (!m_asleep) return;
    spdlog::info("[SleepManager] manual wake");
    m_asleep = false;
    emit stateChanged();
    emit waking();
}

void SleepManager::posixSignalHandler(int sig) {
    if (!s_instance) return;
    char b = (sig == SIGUSR1) ? 'S' : 'W';
    ssize_t r = ::write(s_instance->m_sigPipe[1], &b, 1);
    (void)r;
}

void SleepManager::installSignalHandlers() {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, m_sigPipe) != 0) {
        spdlog::warn("[SleepManager] socketpair failed — signal triggers disabled");
        return;
    }
    std::signal(SIGUSR1, &SleepManager::posixSignalHandler);
    std::signal(SIGUSR2, &SleepManager::posixSignalHandler);
    m_sigNotifier = new QSocketNotifier(m_sigPipe[0], QSocketNotifier::Read, this);
    connect(m_sigNotifier, &QSocketNotifier::activated, this,
            [this](QSocketDescriptor fd){
        char b = 0;
        ssize_t r = ::read(fd, &b, 1);
        (void)r;
        if (b == 'S') requestSleep();
        else if (b == 'W') requestWake();
    });
    spdlog::info("[SleepManager] SIGUSR1=sleep / SIGUSR2=wake handlers installed");
}
