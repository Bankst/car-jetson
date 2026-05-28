#pragma once

#include <QObject>
#include <QDBusConnection>
#include <QTimer>
#include <QString>
#include <QVariantList>

/// Manages BlueZ adapter + paired/discovered devices via D-Bus.
/// Exposed to QML for the Settings > Media page.
class BluetoothManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString adapterName READ adapterName WRITE setAdapterName NOTIFY adapterInfoChanged)
    Q_PROPERTY(QString adapterAddress READ adapterAddress NOTIFY adapterInfoChanged)
    Q_PROPERTY(bool discoverable READ discoverable WRITE setDiscoverable NOTIFY adapterInfoChanged)
    Q_PROPERTY(bool powered READ powered WRITE setPowered NOTIFY adapterInfoChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY adapterInfoChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
    explicit BluetoothManager(QObject* parent = nullptr);

    QString adapterName() const { return m_adapterName; }
    QString adapterAddress() const { return m_adapterAddress; }
    bool discoverable() const { return m_discoverable; }
    bool powered() const { return m_powered; }
    bool discovering() const { return m_discovering; }
    QVariantList devices() const { return m_devices; }

    void setAdapterName(const QString& name);
    void setDiscoverable(bool on);
    void setPowered(bool on);

    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    Q_INVOKABLE void removePairing(const QString& mac);
    Q_INVOKABLE void connectDevice(const QString& mac);
    Q_INVOKABLE void disconnectDevice(const QString& mac);

signals:
    void adapterInfoChanged();
    void devicesChanged();

private slots:
    void refreshAdapter();
    void refreshDevices();

private:

    /// Set a property on org.bluez.Adapter1 via D-Bus Properties interface.
    void setAdapterProperty(const QString& prop, const QVariant& value);

    /// Convert a MAC address (AA:BB:CC:DD:EE:FF) to a BlueZ D-Bus object path.
    static QString macToPath(const QString& mac);

    /// Read a single property from a BlueZ D-Bus object.
    QVariant getProperty(const QString& path, const QString& iface, const QString& prop);

    QDBusConnection m_bus;
    QTimer m_pollTimer;

    QString m_adapterName;
    QString m_adapterAddress;
    bool m_discoverable = false;
    bool m_powered = false;
    bool m_discovering = false;
    QVariantList m_devices;

    static constexpr const char* kService = "org.bluez";
    static constexpr const char* kAdapterPath = "/org/bluez/hci0";
    static constexpr const char* kAdapterIface = "org.bluez.Adapter1";
    static constexpr const char* kDeviceIface = "org.bluez.Device1";
    static constexpr const char* kPropsIface = "org.freedesktop.DBus.Properties";
    static constexpr const char* kObjectManagerIface = "org.freedesktop.DBus.ObjectManager";
};
