#include "SystemInfo.h"
#include <QFile>
#include <QTextStream>
#include <QDir>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <sys/utsname.h>

SystemInfo::SystemInfo(QObject* parent) : QObject(parent) {
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, &SystemInfo::refresh);
    m_timer.start();
    refresh();
}

QString SystemInfo::readFile(const char* path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QTextStream(&f).readAll().trimmed();
}

void SystemInfo::refresh() {
    m_hostname = readFile("/etc/hostname");
    if (m_hostname.isEmpty()) {
        struct utsname u;
        if (uname(&u) == 0) m_hostname = QString::fromLatin1(u.nodename);
    }

    QString up = readFile("/proc/uptime");
    if (!up.isEmpty()) {
        double secs = up.split(' ').first().toDouble();
        int d = static_cast<int>(secs) / 86400;
        int h = (static_cast<int>(secs) % 86400) / 3600;
        int m = (static_cast<int>(secs) % 3600) / 60;
        m_uptime = QStringLiteral("%1d %2h %3m").arg(d).arg(h).arg(m);
    }

    QString temp = readFile("/sys/class/thermal/thermal_zone0/temp");
    if (!temp.isEmpty()) m_cpuTemp = QString::number(temp.toInt() / 1000);

    struct utsname u;
    if (uname(&u) == 0) m_kernel = QString::fromLatin1(u.release);

    QString meminfo = readFile("/proc/meminfo");
    if (!meminfo.isEmpty()) {
        long total = 0, avail = 0;
        for (const auto& line : meminfo.split('\n')) {
            if (line.startsWith("MemTotal:"))
                total = line.split(QRegularExpression("\\s+")).value(1).toLong();
            else if (line.startsWith("MemAvailable:"))
                avail = line.split(QRegularExpression("\\s+")).value(1).toLong();
        }
        if (total > 0) {
            m_memUsage = static_cast<float>(total - avail) / total;
            double usedGB = (total - avail) / 1048576.0;
            double totalGB = total / 1048576.0;
            m_memText = QStringLiteral("%1 / %2 GB")
                .arg(usedGB, 0, 'f', 1).arg(totalGB, 0, 'f', 1);
        }
    }

    m_netIfaces.clear();
    for (const auto& iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        QString name = iface.name();
        for (const auto& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                m_netIfaces.append(name + ": " + entry.ip().toString());
            }
        }
        if (iface.addressEntries().isEmpty()) {
            m_netIfaces.append(name + ": down");
        }
    }

    emit updated();
}
