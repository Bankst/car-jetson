#pragma once

#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusConnection>
#include <QTimer>
#include <QMutex>
#include <QWaitCondition>
#include <QString>
#include <cstdint>

/// BlueZ D-Bus pairing agent.
///
/// Registers at /banks/agent on the system bus as an org.bluez.Agent1 with
/// KeyboardDisplay capability. When BlueZ sends a RequestConfirmation, the
/// agent emits confirmationRequested() so the QML UI can show a dialog, then
/// blocks the D-Bus method call until confirmPairing() is invoked (or the
/// 30-second timeout expires).
class BluetoothPairingAgent : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_PROPERTY(bool pairingPending READ pairingPending NOTIFY pairingPendingChanged)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY confirmationRequested)
    Q_PROPERTY(QString passkey READ passkey NOTIFY confirmationRequested)

public:
    explicit BluetoothPairingAgent(QObject* parent = nullptr);
    ~BluetoothPairingAgent() override;

    /// Register with BlueZ AgentManager1. Returns true on success.
    bool registerAgent();

    /// Unregister from BlueZ and unexport the D-Bus object.
    void unregisterAgent();

    bool pairingPending() const { return m_pairingPending; }
    QString deviceName() const { return m_deviceName; }
    QString passkey() const { return m_passkey; }

    void setAutoAcceptForAA(bool enabled) { m_autoAcceptAA = enabled; }

signals:
    /// Emitted on the GUI thread when BlueZ sends RequestConfirmation.
    void confirmationRequested(const QString& device, const QString& passkey);

    /// Emitted when pairing completes (accepted or rejected).
    void pairingComplete(const QString& device, bool accepted);

    /// Emitted when BlueZ cancels an in-progress pairing.
    void pairingCancelled();

    void pairingPendingChanged();

public slots:
    /// Called from QML to accept or reject the pairing.
    Q_INVOKABLE void confirmPairing(bool accept);

private:
    friend class AgentAdaptor;

    /// Called by the adaptor on the D-Bus worker thread. Sets delayed reply,
    /// blocks until the user responds or the timeout fires, then sends the
    /// reply. Returns true = accepted.
    bool handleConfirmation(const QDBusObjectPath& device, uint32_t passkey,
                            const QDBusMessage& dbusMsg);

    /// Called by the adaptor when BlueZ cancels.
    void handleCancel();

    /// Called by the adaptor on Release.
    void handleRelease();

    /// Resolve a BlueZ device object path to a friendly name via D-Bus
    /// properties. Falls back to the MAC address on failure.
    QString resolveDeviceName(const QDBusObjectPath& path);

    void setPairingPending(bool v);

    QDBusConnection m_bus;
    bool m_registered = false;
    bool m_pairingPending = false;

    // Synchronization between D-Bus thread (blocked in handleConfirmation)
    // and GUI thread (confirmPairing slot).
    QMutex m_mutex;
    QWaitCondition m_cond;
    bool m_responded = false;
    bool m_accepted = false;

    QString m_deviceName;
    QString m_passkey;
    QDBusMessage m_pendingReply;
    bool m_autoAcceptAA = false;

    static constexpr int kTimeoutMs = 30000;
    static constexpr const char* kAgentPath = "/banks/agent";
};

/// QDBusAbstractAdaptor that exports the org.bluez.Agent1 interface.
/// Installed on the BluetoothPairingAgent QObject.
class AgentAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    explicit AgentAdaptor(BluetoothPairingAgent* agent);

public slots:
    Q_NOREPLY void Release();
    Q_NOREPLY void Cancel();
    void RequestConfirmation(const QDBusObjectPath& device, uint passkey);
    uint RequestPasskey(const QDBusObjectPath& device);
    QString RequestPinCode(const QDBusObjectPath& device);
    Q_NOREPLY void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
    QString RequestAuthorization(const QDBusObjectPath& device);

private:
    BluetoothPairingAgent* m_agent;
};
