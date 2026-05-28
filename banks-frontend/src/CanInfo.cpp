#include "CanInfo.h"

#include <linux/if_link.h>
#include <libsocketcan.h>
#include <linux/can/netlink.h>

#include <fcntl.h>
#include <unistd.h>

#include <QByteArray>
#include <QFile>

extern "C" unsigned int if_nametoindex(const char* ifname);

#ifndef ARPHRD_CAN
#define ARPHRD_CAN 280
#endif

namespace {
// libsocketcan writes nlattr lookup failures straight to stderr. Swallow it.
struct StderrSilencer {
    int saved = -1;
    int devnull = -1;
    StderrSilencer() {
        ::fflush(stderr);
        saved = ::dup(2);
        devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) ::dup2(devnull, 2);
    }
    ~StderrSilencer() {
        ::fflush(stderr);
        if (saved >= 0) { ::dup2(saved, 2); ::close(saved); }
        if (devnull >= 0) ::close(devnull);
    }
};

QString stateName(int s) {
    switch (s) {
        case CAN_STATE_ERROR_ACTIVE:  return "Error Active";
        case CAN_STATE_ERROR_WARNING: return "Error Warning";
        case CAN_STATE_ERROR_PASSIVE: return "Error Passive";
        case CAN_STATE_BUS_OFF:       return "Bus Off";
        case CAN_STATE_STOPPED:       return "Stopped";
        case CAN_STATE_SLEEPING:      return "Sleeping";
        default:                      return "Unknown";
    }
}
}

CanInfo::CanInfo(QObject* parent) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &CanInfo::refresh);
    m_timer.start(1000);
    refresh();
}

void CanInfo::setIface(const QString& v) {
    if (v == m_iface) return;
    m_iface = v;
    emit ifaceChanged();
    refresh();
}

void CanInfo::refresh() {
    const QByteArray name = m_iface.toLatin1();
    const char* n = name.constData();

    if (if_nametoindex(n) == 0) {
        if (m_available) { m_available = false; emit updated(); }
        return;
    }
    // Confirm it's a CAN-typed netdev — avoids libsocketcan stderr spam
    // on non-CAN interfaces (it logs nlattr lookup misses directly).
    QFile typeFile(QStringLiteral("/sys/class/net/%1/type").arg(m_iface));
    bool isCan = false;
    if (typeFile.open(QIODevice::ReadOnly)) {
        isCan = typeFile.readAll().trimmed().toInt() == ARPHRD_CAN;
    }
    if (!isCan) {
        if (m_available) { m_available = false; emit updated(); }
        return;
    }

    can_bittiming bt{};
    int st = -1;
    can_berr_counter bc{};
    rtnl_link_stats64 ls{};
    int rcBt, rcSt, rcBc, rcLs;
    {
        StderrSilencer hush;
        rcBt = can_get_bittiming(n, &bt);
        rcSt = can_get_state(n, &st);
        rcBc = can_get_berr_counter(n, &bc);
        rcLs = can_get_link_stats(n, &ls);
    }
    quint32 bitrate = (rcBt == 0) ? bt.bitrate : 0;
    QString stateStr = (rcSt == 0) ? stateName(st) : QStringLiteral("Unknown");
    quint8 txec = (rcBc == 0) ? bc.txerr : 0;
    quint8 rxec = (rcBc == 0) ? bc.rxerr : 0;
    quint64 txp = 0, rxp = 0, txe = 0, rxe = 0;
    if (rcLs == 0) {
        txp = ls.tx_packets; rxp = ls.rx_packets;
        txe = ls.tx_errors;  rxe = ls.rx_errors;
    }

    m_available = true;
    m_bitrate = bitrate;
    m_state = stateStr;
    m_txErrCounter = txec; m_rxErrCounter = rxec;
    m_txPackets = txp; m_rxPackets = rxp; m_txErrors = txe; m_rxErrors = rxe;
    emit updated();
}
