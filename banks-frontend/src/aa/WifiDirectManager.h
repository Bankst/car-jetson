#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class WifiDirectManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString groupInterface READ groupInterface NOTIFY activeChanged)
    Q_PROPERTY(QString goAddress READ goAddress NOTIFY activeChanged)

public:
    explicit WifiDirectManager(QObject* parent = nullptr);
    ~WifiDirectManager() override;

    bool start();
    void stop();
    bool active() const { return m_active; }
    QString groupInterface() const { return m_groupIface; }
    QString goAddress() const { return m_goAddr; }
    QString ssid() const { return m_ssid; }
    QString passphrase() const { return m_passphrase; }

signals:
    void activeChanged();

private:
    bool createP2PGroup();
    bool waitForInterface(int timeoutMs = 5000);
    bool assignAddress();
    bool startDhcp();
    void stopDhcp();
    void queryGroupInfo();
    QString findWpaSupplicantInterface();

    bool m_active = false;
    QString m_wpaIfacePath;
    QString m_groupIface;
    QString m_goAddr = QStringLiteral("192.168.49.1");
    QString m_ssid;
    QString m_passphrase;
    QProcess* m_dnsmasq = nullptr;
};
