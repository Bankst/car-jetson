#include "BluetoothPairingAgent.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDebug>
#include <QMetaObject>
#include <QMutexLocker>
#include <QElapsedTimer>

#include <f1x/openauto/Common/Log.hpp>

// ---------------------------------------------------------------------------
// AgentAdaptor
// ---------------------------------------------------------------------------

AgentAdaptor::AgentAdaptor(BluetoothPairingAgent* agent)
    : QDBusAbstractAdaptor(agent)
    , m_agent(agent)
{}

void AgentAdaptor::Release() {
    OPENAUTO_LOG(info) << "[BTPairingAgent] Release() from BlueZ";
    m_agent->handleRelease();
}

void AgentAdaptor::Cancel() {
    OPENAUTO_LOG(info) << "[BTPairingAgent] Cancel() from BlueZ";
    m_agent->handleCancel();
}

void AgentAdaptor::RequestConfirmation(const QDBusObjectPath& device, uint passkey) {
    OPENAUTO_LOG(info) << "[BTPairingAgent] RequestConfirmation device="
                       << device.path().toStdString() << " passkey=" << passkey;

    // Tell Qt D-Bus we will send the reply ourselves (after user interaction).
    m_agent->setDelayedReply(true);
    QDBusMessage msg = m_agent->message();

    m_agent->handleConfirmation(device, passkey, msg);
    // Reply is sent inside handleConfirmation — nothing more to do here.
}

uint AgentAdaptor::RequestPasskey(const QDBusObjectPath& device) {
    Q_UNUSED(device);
    OPENAUTO_LOG(info) << "[BTPairingAgent] RequestPasskey — returning 0 (unsupported)";
    return 0;
}

QString AgentAdaptor::RequestPinCode(const QDBusObjectPath& device) {
    Q_UNUSED(device);
    OPENAUTO_LOG(info) << "[BTPairingAgent] RequestPinCode — returning 0000";
    return QStringLiteral("0000");
}

void AgentAdaptor::AuthorizeService(const QDBusObjectPath& device, const QString& uuid) {
    OPENAUTO_LOG(info) << "[BTPairingAgent] AuthorizeService device="
                       << device.path().toStdString() << " uuid=" << uuid.toStdString();
    // Auto-authorize all services.
}

QString AgentAdaptor::RequestAuthorization(const QDBusObjectPath& device) {
    Q_UNUSED(device);
    OPENAUTO_LOG(info) << "[BTPairingAgent] RequestAuthorization — accepting";
    return {};
}

// ---------------------------------------------------------------------------
// BluetoothPairingAgent
// ---------------------------------------------------------------------------

BluetoothPairingAgent::BluetoothPairingAgent(QObject* parent)
    : QObject(parent)
    , m_bus(QDBusConnection::systemBus())
{
    // The adaptor auto-registers its interface on our QObject when we export
    // ourselves to the bus.
    new AgentAdaptor(this);
}

BluetoothPairingAgent::~BluetoothPairingAgent() {
    unregisterAgent();
}

bool BluetoothPairingAgent::registerAgent() {
    if (m_registered) return true;

    if (!m_bus.isConnected()) {
        OPENAUTO_LOG(error) << "[BTPairingAgent] system bus not connected";
        return false;
    }

    // Export our QObject (with the AgentAdaptor) at the agent path.
    if (!m_bus.registerObject(QLatin1String(kAgentPath), this)) {
        OPENAUTO_LOG(error) << "[BTPairingAgent] failed to register object at "
                            << kAgentPath << ": "
                            << m_bus.lastError().message().toStdString();
        return false;
    }

    // Call org.bluez.AgentManager1.RegisterAgent(path, capability)
    QDBusInterface mgr(QStringLiteral("org.bluez"),
                       QStringLiteral("/org/bluez"),
                       QStringLiteral("org.bluez.AgentManager1"),
                       m_bus);
    if (!mgr.isValid()) {
        OPENAUTO_LOG(error) << "[BTPairingAgent] AgentManager1 interface not available: "
                            << mgr.lastError().message().toStdString();
        m_bus.unregisterObject(QLatin1String(kAgentPath));
        return false;
    }

    mgr.setTimeout(3000);
    QDBusReply<void> reply = mgr.call(
        QStringLiteral("RegisterAgent"),
        QDBusObjectPath(QLatin1String(kAgentPath)),
        QStringLiteral("KeyboardDisplay"));

    if (!reply.isValid()) {
        OPENAUTO_LOG(error) << "[BTPairingAgent] RegisterAgent failed: "
                            << reply.error().message().toStdString();
        m_bus.unregisterObject(QLatin1String(kAgentPath));
        return false;
    }

    // Request to be the default agent.
    QDBusReply<void> defReply = mgr.call(
        QStringLiteral("RequestDefaultAgent"),
        QDBusObjectPath(QLatin1String(kAgentPath)));
    if (!defReply.isValid()) {
        OPENAUTO_LOG(warning) << "[BTPairingAgent] RequestDefaultAgent failed: "
                              << defReply.error().message().toStdString()
                              << " (non-fatal — another agent may be default)";
    }

    m_registered = true;
    OPENAUTO_LOG(info) << "[BTPairingAgent] registered at " << kAgentPath
                       << " (KeyboardDisplay)";
    return true;
}

void BluetoothPairingAgent::unregisterAgent() {
    if (!m_registered) return;

    // Wake any blocked D-Bus call.
    {
        QMutexLocker lock(&m_mutex);
        m_responded = true;
        m_accepted = false;
        m_cond.wakeAll();
    }

    QDBusInterface mgr(QStringLiteral("org.bluez"),
                       QStringLiteral("/org/bluez"),
                       QStringLiteral("org.bluez.AgentManager1"),
                       m_bus);
    if (mgr.isValid()) {
        mgr.call(QStringLiteral("UnregisterAgent"),
                 QDBusObjectPath(QLatin1String(kAgentPath)));
    }

    m_bus.unregisterObject(QLatin1String(kAgentPath));
    m_registered = false;
    OPENAUTO_LOG(info) << "[BTPairingAgent] unregistered";
}

bool BluetoothPairingAgent::handleConfirmation(const QDBusObjectPath& device,
                                                uint32_t passkey,
                                                const QDBusMessage& dbusMsg) {
    QString name = resolveDeviceName(device);
    QString pin = QString::number(passkey).rightJustified(6, QLatin1Char('0'));

    m_pendingReply = dbusMsg;
    m_deviceName = name;
    m_passkey = pin;

    if (m_autoAcceptAA) {
        OPENAUTO_LOG(info) << "[BTPairingAgent] AA-initiated pairing from "
                           << name.toStdString() << " — auto-accepting";
        m_bus.send(m_pendingReply.createReply());
        emit pairingComplete(name, true);
        return true;
    }

    setPairingPending(true);
    emit confirmationRequested(name, pin);

    QTimer::singleShot(kTimeoutMs, this, [this]{
        if (m_pairingPending) {
            OPENAUTO_LOG(warning) << "[BTPairingAgent] confirmation timed out — rejecting";
            confirmPairing(false);
        }
    });

    OPENAUTO_LOG(info) << "[BTPairingAgent] confirmation dialog shown for "
                       << name.toStdString();
    return true;
}

void BluetoothPairingAgent::handleCancel() {
    if (m_pairingPending) {
        m_bus.send(m_pendingReply.createErrorReply(
            QStringLiteral("org.bluez.Error.Canceled"),
            QStringLiteral("Pairing cancelled")));
        setPairingPending(false);
        emit pairingCancelled();
    }
}

void BluetoothPairingAgent::handleRelease() {
    {
        QMutexLocker lock(&m_mutex);
        m_responded = true;
        m_accepted = false;
        m_cond.wakeAll();
    }

    QMetaObject::invokeMethod(this, [this]{
        setPairingPending(false);
        m_registered = false;
    }, Qt::QueuedConnection);
}

void BluetoothPairingAgent::confirmPairing(bool accept) {
    OPENAUTO_LOG(info) << "[BTPairingAgent] confirmPairing(" << accept << ")";

    if (!m_pairingPending) return;

    if (accept) {
        m_bus.send(m_pendingReply.createReply());
    } else {
        m_bus.send(m_pendingReply.createErrorReply(
            QStringLiteral("org.bluez.Error.Rejected"),
            QStringLiteral("Pairing rejected by user")));
    }

    setPairingPending(false);
    emit pairingComplete(m_deviceName, accept);
}

QString BluetoothPairingAgent::resolveDeviceName(const QDBusObjectPath& path) {
    // Query org.bluez device properties for "Alias" (friendly name).
    QDBusInterface dev(QStringLiteral("org.bluez"),
                       path.path(),
                       QStringLiteral("org.freedesktop.DBus.Properties"),
                       m_bus);
    if (dev.isValid()) {
        QDBusReply<QDBusVariant> reply = dev.call(
            QStringLiteral("Get"),
            QStringLiteral("org.bluez.Device1"),
            QStringLiteral("Alias"));
        if (reply.isValid()) {
            QString alias = reply.value().variant().toString();
            if (!alias.isEmpty()) return alias;
        }
    }

    // Fallback: extract MAC from path (e.g. /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF).
    QString p = path.path();
    int idx = p.lastIndexOf(QLatin1String("dev_"));
    if (idx >= 0) {
        QString mac = p.mid(idx + 4).replace(QLatin1Char('_'), QLatin1Char(':'));
        return mac;
    }
    return path.path();
}

void BluetoothPairingAgent::setPairingPending(bool v) {
    if (m_pairingPending != v) {
        m_pairingPending = v;
        emit pairingPendingChanged();
    }
}
