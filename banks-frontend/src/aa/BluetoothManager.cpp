#include "BluetoothManager.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QVariantMap>
#include <QDebug>

BluetoothManager::BluetoothManager(QObject* parent)
    : QObject(parent)
    , m_bus(QDBusConnection::systemBus())
{
    // Poll adapter + devices periodically. BlueZ PropertiesChanged signals
    // are unreliable across bus restarts, and polling every 3s is cheap.
    m_pollTimer.setInterval(3000);
    connect(&m_pollTimer, &QTimer::timeout, this, [this]{
        refreshAdapter();
        refreshDevices();
    });

    // Also listen for InterfacesAdded/Removed for immediate device list updates.
    m_bus.connect(
        QLatin1String(kService), QStringLiteral("/"),
        QLatin1String(kObjectManagerIface),
        QStringLiteral("InterfacesAdded"),
        this, SLOT(refreshDevices()));
    m_bus.connect(
        QLatin1String(kService), QStringLiteral("/"),
        QLatin1String(kObjectManagerIface),
        QStringLiteral("InterfacesRemoved"),
        this, SLOT(refreshDevices()));

    // Listen for PropertiesChanged on the adapter for discoverable/powered toggles.
    m_bus.connect(
        QLatin1String(kService),
        QLatin1String(kAdapterPath),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"),
        this, SLOT(refreshAdapter()));

    // Initial load.
    refreshAdapter();
    refreshDevices();
    m_pollTimer.start();
}

// ---------------------------------------------------------------------------
// Adapter
// ---------------------------------------------------------------------------

void BluetoothManager::refreshAdapter() {
    bool changed = false;

    auto name = getProperty(kAdapterPath, kAdapterIface, QStringLiteral("Alias")).toString();
    if (name != m_adapterName) { m_adapterName = name; changed = true; }

    auto addr = getProperty(kAdapterPath, kAdapterIface, QStringLiteral("Address")).toString();
    if (addr != m_adapterAddress) { m_adapterAddress = addr; changed = true; }

    bool disc = getProperty(kAdapterPath, kAdapterIface, QStringLiteral("Discoverable")).toBool();
    if (disc != m_discoverable) { m_discoverable = disc; changed = true; }

    bool pwr = getProperty(kAdapterPath, kAdapterIface, QStringLiteral("Powered")).toBool();
    if (pwr != m_powered) { m_powered = pwr; changed = true; }

    bool scanning = getProperty(kAdapterPath, kAdapterIface, QStringLiteral("Discovering")).toBool();
    if (scanning != m_discovering) { m_discovering = scanning; changed = true; }

    if (changed) emit adapterInfoChanged();
}

void BluetoothManager::setAdapterName(const QString& name) {
    if (name == m_adapterName) return;
    setAdapterProperty(QStringLiteral("Alias"), name);
    m_adapterName = name;
    emit adapterInfoChanged();
}

void BluetoothManager::setDiscoverable(bool on) {
    if (on == m_discoverable) return;
    setAdapterProperty(QStringLiteral("Discoverable"), on);
    m_discoverable = on;
    emit adapterInfoChanged();
}

void BluetoothManager::setPowered(bool on) {
    if (on == m_powered) return;
    setAdapterProperty(QStringLiteral("Powered"), on);
    m_powered = on;
    emit adapterInfoChanged();
}

void BluetoothManager::setAdapterProperty(const QString& prop, const QVariant& value) {
    QDBusInterface iface(QLatin1String(kService), QLatin1String(kAdapterPath),
                         QLatin1String(kPropsIface), m_bus);
    if (!iface.isValid()) {
        qWarning("[BTManager] adapter D-Bus interface unavailable");
        return;
    }
    iface.call(QStringLiteral("Set"),
               QString::fromLatin1(kAdapterIface), prop,
               QVariant::fromValue(QDBusVariant(value)));
}

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

void BluetoothManager::refreshDevices() {
    QDBusInterface objMgr(QLatin1String(kService), QStringLiteral("/"),
                          QLatin1String(kObjectManagerIface), m_bus);
    if (!objMgr.isValid()) {
        if (!m_devices.isEmpty()) {
            m_devices.clear();
            emit devicesChanged();
        }
        return;
    }

    QDBusMessage reply = objMgr.call(QStringLiteral("GetManagedObjects"));
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return;
    }

    QVariantList newDevices;

    // GetManagedObjects returns a{oa{sa{sv}}}. Use QDBusObjectPath-keyed map.
    // Let Qt handle the complex deserialization by converting the whole reply
    // to a variant map hierarchy.
    using ManagedObjects = QMap<QDBusObjectPath, QMap<QString, QVariantMap>>;
    ManagedObjects objects;
    reply.arguments().at(0).value<QDBusArgument>() >> objects;

    for (auto it = objects.constBegin(); it != objects.constEnd(); ++it) {
        const QString path = it.key().path();
        const auto& ifaces = it.value();

        if (!ifaces.contains(QLatin1String(kDeviceIface))) continue;

        const QVariantMap& devProps = ifaces.value(QLatin1String(kDeviceIface));
        QString name = devProps.value(QStringLiteral("Alias")).toString();
        if (name.isEmpty()) name = devProps.value(QStringLiteral("Name")).toString();
        QString mac = devProps.value(QStringLiteral("Address")).toString();
        bool paired = devProps.value(QStringLiteral("Paired")).toBool();
        bool connected = devProps.value(QStringLiteral("Connected")).toBool();

        QVariantMap entry;
        entry[QStringLiteral("name")] = name.isEmpty() ? mac : name;
        entry[QStringLiteral("mac")] = mac;
        entry[QStringLiteral("paired")] = paired;
        entry[QStringLiteral("connected")] = connected;
        entry[QStringLiteral("path")] = path;
        newDevices.append(entry);
    }

    if (newDevices != m_devices) {
        m_devices = newDevices;
        emit devicesChanged();
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void BluetoothManager::startDiscovery() {
    QDBusInterface adapter(QLatin1String(kService), QLatin1String(kAdapterPath),
                           QLatin1String(kAdapterIface), m_bus);
    if (adapter.isValid()) {
        adapter.call(QStringLiteral("StartDiscovery"));
        qInfo("[BTManager] discovery started");
        // Refresh immediately to pick up Discovering=true
        refreshAdapter();
    } else {
        qWarning("[BTManager] adapter not available for StartDiscovery");
    }
}

void BluetoothManager::stopDiscovery() {
    QDBusInterface adapter(QLatin1String(kService), QLatin1String(kAdapterPath),
                           QLatin1String(kAdapterIface), m_bus);
    if (adapter.isValid()) {
        adapter.call(QStringLiteral("StopDiscovery"));
        qInfo("[BTManager] discovery stopped");
        refreshAdapter();
    }
}

void BluetoothManager::removePairing(const QString& mac) {
    QString devPath = macToPath(mac);
    QDBusInterface adapter(QLatin1String(kService), QLatin1String(kAdapterPath),
                           QLatin1String(kAdapterIface), m_bus);
    if (adapter.isValid()) {
        QDBusReply<void> reply = adapter.call(
            QStringLiteral("RemoveDevice"),
            QVariant::fromValue(QDBusObjectPath(devPath)));
        if (!reply.isValid()) {
            qWarning("[BTManager] RemoveDevice(%s) failed: %s",
                     qPrintable(devPath), qPrintable(reply.error().message()));
        } else {
            qInfo("[BTManager] removed %s", qPrintable(mac));
        }
    }
    refreshDevices();
}

void BluetoothManager::connectDevice(const QString& mac) {
    QString devPath = macToPath(mac);
    QDBusInterface dev(QLatin1String(kService), devPath,
                       QLatin1String(kDeviceIface), m_bus);
    if (dev.isValid()) {
        QDBusReply<void> reply = dev.call(QStringLiteral("Connect"));
        if (!reply.isValid()) {
            qWarning("[BTManager] Connect(%s) failed: %s",
                     qPrintable(mac), qPrintable(reply.error().message()));
        } else {
            qInfo("[BTManager] connecting %s", qPrintable(mac));
        }
    }
    refreshDevices();
}

void BluetoothManager::disconnectDevice(const QString& mac) {
    QString devPath = macToPath(mac);
    QDBusInterface dev(QLatin1String(kService), devPath,
                       QLatin1String(kDeviceIface), m_bus);
    if (dev.isValid()) {
        QDBusReply<void> reply = dev.call(QStringLiteral("Disconnect"));
        if (!reply.isValid()) {
            qWarning("[BTManager] Disconnect(%s) failed: %s",
                     qPrintable(mac), qPrintable(reply.error().message()));
        } else {
            qInfo("[BTManager] disconnecting %s", qPrintable(mac));
        }
    }
    refreshDevices();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

QString BluetoothManager::macToPath(const QString& mac) {
    // AA:BB:CC:DD:EE:FF -> /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
    QString cleaned = mac;
    cleaned.replace(QLatin1Char(':'), QLatin1Char('_'));
    return QStringLiteral("/org/bluez/hci0/dev_") + cleaned;
}

QVariant BluetoothManager::getProperty(const QString& path, const QString& iface,
                                        const QString& prop) {
    QDBusInterface propsIface(QLatin1String(kService), path,
                              QLatin1String(kPropsIface), m_bus);
    if (!propsIface.isValid()) return {};
    QDBusReply<QDBusVariant> reply = propsIface.call(QStringLiteral("Get"), iface, prop);
    if (reply.isValid()) return reply.value().variant();
    return {};
}
