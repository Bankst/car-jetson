#include "LogCapture.h"
#include <QDateTime>
#include <QMetaObject>
#include <QThread>

LogCapture* LogCapture::s_instance = nullptr;
QtMessageHandler LogCapture::s_previousHandler = nullptr;

LogCapture::LogCapture(QObject* parent)
    : QObject(parent)
{
}

LogCapture* LogCapture::instance()
{
    return s_instance;
}

void LogCapture::install()
{
    if (!s_instance)
        s_instance = new LogCapture();
    s_previousHandler = qInstallMessageHandler(messageHandler);
}

void LogCapture::messageHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    static thread_local bool inHandler = false;
    if (inHandler) return;
    inHandler = true;

    if (s_previousHandler)
        s_previousHandler(type, ctx, msg);

    if (s_instance)
        s_instance->append(type, msg);

    inHandler = false;
}

void LogCapture::append(QtMsgType type, const QString& msg)
{
    int level;
    const char* tag;
    switch (type) {
    case QtDebugMsg:    level = 0; tag = "DBG"; break;
    case QtInfoMsg:     level = 1; tag = "INF"; break;
    case QtWarningMsg:  level = 2; tag = "WRN"; break;
    case QtCriticalMsg: level = 3; tag = "ERR"; break;
    case QtFatalMsg:    level = 3; tag = "FTL"; break;
    default:            level = 0; tag = "???"; break;
    }

    QString ts = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    QString line = QStringLiteral("[%1] [%2] %3").arg(ts, QString::fromLatin1(tag), msg);

    Entry entry{level, line};

    QMutexLocker lock(&m_mutex);
    m_entries.append(std::move(entry));
    while (m_entries.size() > MaxEntries)
        m_entries.removeFirst();
    lock.unlock();

    // Throttle UI updates — batch via a coalescing timer instead of
    // emitting per-message (prevents recursive Qt scene graph logging).
    if (!m_dirty.exchange(true)) {
        QMetaObject::invokeMethod(this, [this]() {
            m_dirty = false;
            emit messagesChanged();
        }, Qt::QueuedConnection);
    }
}

QStringList LogCapture::messages() const
{
    QMutexLocker lock(&m_mutex);
    return filtered();
}

int LogCapture::totalCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_entries.size();
}

QStringList LogCapture::filtered() const
{
    // m_mutex must be held by caller
    QStringList result;
    result.reserve(m_entries.size());
    for (const auto& e : m_entries) {
        if (e.level >= m_filterLevel)
            result.append(e.formatted);
    }
    return result;
}

void LogCapture::setAutoScroll(bool v)
{
    if (m_autoScroll != v) {
        m_autoScroll = v;
        emit autoScrollChanged();
    }
}

void LogCapture::setFilterLevel(int v)
{
    if (v < 0) v = 0;
    if (v > 3) v = 3;
    if (m_filterLevel != v) {
        m_filterLevel = v;
        emit filterLevelChanged();
        emit messagesChanged();
    }
}

void LogCapture::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        m_entries.clear();
    }
    emit messagesChanged();
}

void LogCapture::copyToClipboard()
{
    QStringList lines;
    {
        QMutexLocker lock(&m_mutex);
        lines = filtered();
    }
    auto* clipboard = QGuiApplication::clipboard();
    if (clipboard)
        clipboard->setText(lines.join(QChar('\n')));
}
