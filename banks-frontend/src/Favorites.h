#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class Favorites : public QObject {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    explicit Favorites(QObject* parent = nullptr);

    int count() const { return m_set.size(); }
    bool contains(const QString& file) const { return m_set.contains(file); }
    QStringList list() const;            // sorted

    Q_INVOKABLE void add(const QString& file);
    Q_INVOKABLE void remove(const QString& file);
    Q_INVOKABLE bool toggle(const QString& file);  // returns new state
    Q_INVOKABLE void clear();

    QString path() const { return m_path; }

    // Last-played preset filename (persisted alongside favorites).
    QString lastPreset() const { return m_lastPreset; }
    void    setLastPreset(const QString& file);

signals:
    void changed();

private:
    void load();
    void save();

    QSet<QString> m_set;
    QString       m_path;
    QString       m_lastPreset;
};
