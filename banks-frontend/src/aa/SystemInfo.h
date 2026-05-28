#pragma once

#include <QObject>
#include <QTimer>
#include <QString>

class SystemInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString hostname READ hostname NOTIFY updated)
    Q_PROPERTY(QString uptime READ uptime NOTIFY updated)
    Q_PROPERTY(QString cpuTemp READ cpuTemp NOTIFY updated)
    Q_PROPERTY(QString kernelVersion READ kernelVersion NOTIFY updated)
    Q_PROPERTY(float memUsage READ memUsage NOTIFY updated)
    Q_PROPERTY(QString memText READ memText NOTIFY updated)
    Q_PROPERTY(QStringList netIfaces READ netIfaces NOTIFY updated)

public:
    explicit SystemInfo(QObject* parent = nullptr);

    QString hostname() const { return m_hostname; }
    QString uptime() const { return m_uptime; }
    QString cpuTemp() const { return m_cpuTemp; }
    QString kernelVersion() const { return m_kernel; }
    float memUsage() const { return m_memUsage; }
    QString memText() const { return m_memText; }
    QStringList netIfaces() const { return m_netIfaces; }

signals:
    void updated();

private:
    void refresh();
    static QString readFile(const char* path);

    QTimer m_timer;
    QString m_hostname, m_uptime, m_cpuTemp, m_kernel, m_memText;
    float m_memUsage = 0;
    QStringList m_netIfaces;
};
