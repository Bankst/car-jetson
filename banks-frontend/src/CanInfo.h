#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class CanInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString iface READ iface WRITE setIface NOTIFY ifaceChanged)
    Q_PROPERTY(bool available READ available NOTIFY updated)
    Q_PROPERTY(quint32 bitrate READ bitrate NOTIFY updated)
    Q_PROPERTY(QString state READ state NOTIFY updated)
    Q_PROPERTY(quint64 txPackets READ txPackets NOTIFY updated)
    Q_PROPERTY(quint64 rxPackets READ rxPackets NOTIFY updated)
    Q_PROPERTY(quint64 txErrors READ txErrors NOTIFY updated)
    Q_PROPERTY(quint64 rxErrors READ rxErrors NOTIFY updated)
    Q_PROPERTY(quint8 txErrCounter READ txErrCounter NOTIFY updated)
    Q_PROPERTY(quint8 rxErrCounter READ rxErrCounter NOTIFY updated)

public:
    explicit CanInfo(QObject* parent = nullptr);

    QString iface() const { return m_iface; }
    void setIface(const QString& v);
    bool available() const { return m_available; }
    quint32 bitrate() const { return m_bitrate; }
    QString state() const { return m_state; }
    quint64 txPackets() const { return m_txPackets; }
    quint64 rxPackets() const { return m_rxPackets; }
    quint64 txErrors() const { return m_txErrors; }
    quint64 rxErrors() const { return m_rxErrors; }
    quint8 txErrCounter() const { return m_txErrCounter; }
    quint8 rxErrCounter() const { return m_rxErrCounter; }

signals:
    void ifaceChanged();
    void updated();

private:
    void refresh();

    QTimer m_timer;
    QString m_iface = "can0";
    QString m_state;
    bool m_available = false;
    quint32 m_bitrate = 0;
    quint64 m_txPackets = 0, m_rxPackets = 0, m_txErrors = 0, m_rxErrors = 0;
    quint8 m_txErrCounter = 0, m_rxErrCounter = 0;
};
