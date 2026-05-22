#include "Favorites.h"
#include "Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#ifndef BANKS_FRONTEND_FAVORITES_PATH
#  define BANKS_FRONTEND_FAVORITES_PATH ""
#endif

Favorites::Favorites(QObject* parent) : QObject(parent) {
    // Override order: env > compile-time > beside binary.
    QByteArray env = qgetenv("BANKS_FRONTEND_FAVORITES");
    if (!env.isEmpty()) {
        m_path = QString::fromUtf8(env);
    } else {
        QString compile = QString::fromUtf8(BANKS_FRONTEND_FAVORITES_PATH);
        m_path = compile.isEmpty()
            ? QCoreApplication::applicationDirPath() + "/favorites.json"
            : compile;
    }
    qCInfo(logFav) << "favorites file:" << m_path;
    load();
}

QStringList Favorites::list() const {
    QStringList out = m_set.values();
    out.sort();
    return out;
}

void Favorites::add(const QString& file) {
    if (file.isEmpty() || m_set.contains(file)) return;
    m_set.insert(file);
    qCInfo(logFav) << "+ favorited:" << QFileInfo(file).completeBaseName();
    save();
    emit changed();
}

void Favorites::remove(const QString& file) {
    if (!m_set.remove(file)) return;
    qCInfo(logFav) << "- unfavorited:" << QFileInfo(file).completeBaseName();
    save();
    emit changed();
}

bool Favorites::toggle(const QString& file) {
    if (m_set.contains(file)) { remove(file); return false; }
    add(file); return true;
}

void Favorites::clear() {
    if (m_set.isEmpty()) return;
    m_set.clear();
    save();
    emit changed();
}

void Favorites::load() {
    QFile f(m_path);
    if (!f.exists()) { qCInfo(logFav) << "no favorites file yet"; return; }
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(logFav) << "open failed:" << f.errorString();
        return;
    }
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(logFav) << "parse failed:" << err.errorString();
        return;
    }
    const QJsonObject root = doc.object();
    const QJsonArray arr = root.value("presets").toArray();
    for (const auto& v : arr) m_set.insert(v.toString());
    m_lastPreset = root.value("lastPreset").toString();
    qCInfo(logFav) << "loaded" << m_set.size() << "favorites; lastPreset="
                   << (m_lastPreset.isEmpty() ? QStringLiteral("<none>") : m_lastPreset);
}

void Favorites::setLastPreset(const QString& file) {
    if (file == m_lastPreset) return;
    m_lastPreset = file;
    save();
}

void Favorites::save() {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCCritical(logFav) << "open for write failed:" << f.errorString();
        return;
    }
    QJsonArray arr;
    auto sorted = list();
    for (const auto& s : sorted) arr.push_back(s);
    QJsonObject root;
    root.insert("version", 1);
    root.insert("presets", arr);
    if (!m_lastPreset.isEmpty()) root.insert("lastPreset", m_lastPreset);
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit()) qCCritical(logFav) << "commit failed:" << f.errorString();
}
