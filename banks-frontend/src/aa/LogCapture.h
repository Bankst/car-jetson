#pragma once

#include <QObject>
#include <QStringList>
#include <QMutex>
#include <atomic>
#include <QClipboard>
#include <QGuiApplication>

class LogCapture : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(bool autoScroll READ autoScroll WRITE setAutoScroll NOTIFY autoScrollChanged)
    Q_PROPERTY(int filterLevel READ filterLevel WRITE setFilterLevel NOTIFY filterLevelChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY messagesChanged)

public:
    explicit LogCapture(QObject* parent = nullptr);

    static LogCapture* instance();
    static void install();

    QStringList messages() const;
    bool autoScroll() const { return m_autoScroll; }
    int filterLevel() const { return m_filterLevel; }
    int totalCount() const;

    void setAutoScroll(bool v);
    void setFilterLevel(int v);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void copyToClipboard();

signals:
    void messagesChanged();
    void autoScrollChanged();
    void filterLevelChanged();
    void newMessage(const QString& msg);

private:
    struct Entry {
        int level;       // 0=debug, 1=info, 2=warning, 3=critical/fatal
        QString formatted;
    };

    static void messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg);

    void append(QtMsgType type, const QString& msg);
    QStringList filtered() const;

    static LogCapture* s_instance;
    static QtMessageHandler s_previousHandler;

    mutable QMutex m_mutex;
    std::atomic<bool> m_dirty{false};
    QList<Entry> m_entries;
    bool m_autoScroll = true;
    int m_filterLevel = 0;  // 0=all, 1=info+, 2=warn+, 3=error+
    static constexpr int MaxEntries = 1000;
};
