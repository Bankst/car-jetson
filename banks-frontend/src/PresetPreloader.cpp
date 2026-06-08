#include "PresetPreloader.h"
#include "Log.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOffscreenSurface>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QFileInfo>
#include <QCoreApplication>

#include <projectM-4/projectM.h>

// Worker object living on the preloader thread. Owns the hidden GL context
// and projectM instance. All GL and projectM calls happen here.
class PresetPreloader::Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(QOpenGLContext* shareContext)
        : m_shareContext(shareContext) {}

    ~Worker() override {
        teardown();
    }

public slots:
    void init() {
        // Create a context sharing GL objects with the render context.
        m_ctx = new QOpenGLContext();
        m_ctx->setShareContext(m_shareContext);
        m_ctx->setFormat(m_shareContext->format());
        if (!m_ctx->create()) {
            qCWarning(logViz) << "PresetPreloader: failed to create shared GL context";
            delete m_ctx;
            m_ctx = nullptr;
            return;
        }

        m_surface = new QOffscreenSurface();
        m_surface->setFormat(m_ctx->format());
        m_surface->create();

        if (!m_ctx->makeCurrent(m_surface)) {
            qCWarning(logViz) << "PresetPreloader: makeCurrent failed";
            teardown();
            return;
        }

        m_pm = projectm_create();
        if (!m_pm) {
            qCCritical(logViz) << "PresetPreloader: projectm_create failed";
            m_ctx->doneCurrent();
            teardown();
            return;
        }

        // Minimal resolution -- we only need shader compilation, not visuals.
        projectm_set_window_size(m_pm, 64, 64);
        projectm_set_mesh_size(m_pm, 16, 9);
        projectm_set_fps(m_pm, 1);
        projectm_set_preset_locked(m_pm, true);

        m_ctx->doneCurrent();
        m_ready = true;
        qCInfo(logViz) << "PresetPreloader: worker ready, shared ctx=" << (void*)m_ctx;
    }

    void processQueue() {
        if (!m_ready) return;

        QStringList batch;
        {
            QMutexLocker lock(&m_mutex);
            if (m_queue.isEmpty()) return;
            batch = m_queue;
            m_queue.clear();
        }

        if (!m_ctx->makeCurrent(m_surface)) {
            qCWarning(logViz) << "PresetPreloader: makeCurrent failed in processQueue";
            return;
        }

        for (const QString& path : batch) {
            // Check if already compiled (under lock, since clear() touches this).
            {
                QMutexLocker lock(&m_mutex);
                if (m_compiled.contains(path)) continue;
                if (m_cancelled) {
                    m_cancelled = false;
                    break;
                }
            }

            // Load the preset (void return -- silently keeps current if file
            // is unreadable) then render one frame to force GLSL compilation.
            projectm_load_preset_file(m_pm, path.toUtf8().constData(), false);
            projectm_opengl_render_frame(m_pm);

            {
                QMutexLocker lock(&m_mutex);
                m_compiled.insert(path);
            }
            qCDebug(logViz) << "PresetPreloader: compiled" << QFileInfo(path).completeBaseName();
            emit preloaded(path);
        }

        m_ctx->doneCurrent();
    }

    void clearState() {
        QMutexLocker lock(&m_mutex);
        m_queue.clear();
        m_compiled.clear();
        m_cancelled = true;
    }

    void enqueue(const QStringList& paths) {
        QMutexLocker lock(&m_mutex);
        for (const QString& p : paths) {
            if (!m_compiled.contains(p) && !m_queue.contains(p)) {
                m_queue.append(p);
            }
        }
    }

    void setDepth(int d) {
        QMutexLocker lock(&m_mutex);
        m_depth = d;
    }

    int depth() const {
        QMutexLocker lock(&m_mutex);
        return m_depth;
    }

signals:
    void preloaded(const QString& path);

private:
    void teardown() {
        if (m_pm) {
            if (m_ctx && m_ctx->makeCurrent(m_surface)) {
                projectm_destroy(m_pm);
                m_ctx->doneCurrent();
            }
            m_pm = nullptr;
        }
        delete m_surface;
        m_surface = nullptr;
        delete m_ctx;
        m_ctx = nullptr;
        m_ready = false;
    }

    QOpenGLContext*    m_shareContext = nullptr;  // not owned
    QOpenGLContext*    m_ctx = nullptr;
    QOffscreenSurface* m_surface = nullptr;
    projectm_handle   m_pm = nullptr;
    bool              m_ready = false;

    mutable QMutex    m_mutex;
    QStringList       m_queue;
    QSet<QString>     m_compiled;
    int               m_depth = 3;
    bool              m_cancelled = false;
};

// --- PresetPreloader (any thread) ---

PresetPreloader::PresetPreloader(QOpenGLContext* shareContext, QObject* parent)
    : QObject(parent)
{
    m_worker = new Worker(shareContext);
    m_worker->moveToThread(&m_thread);

    connect(&m_thread, &QThread::started, m_worker, &Worker::init);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &Worker::preloaded, this, &PresetPreloader::presetPreloaded);

    m_thread.setObjectName(QStringLiteral("PresetPreloader"));
    m_thread.start(QThread::LowPriority);
}

PresetPreloader::~PresetPreloader() {
    m_worker->clearState();
    m_thread.quit();
    m_thread.wait(3000);
}

void PresetPreloader::setLookaheadDepth(int depth) {
    if (depth < 1) depth = 1;
    if (depth > 20) depth = 20;
    m_worker->setDepth(depth);
}

int PresetPreloader::lookaheadDepth() const {
    return m_worker->depth();
}

void PresetPreloader::preloadPresets(const QStringList& presetPaths) {
    if (presetPaths.isEmpty()) return;
    m_worker->enqueue(presetPaths);
    // Trigger processing on the worker thread.
    QMetaObject::invokeMethod(m_worker, &Worker::processQueue, Qt::QueuedConnection);
}

void PresetPreloader::clear() {
    m_worker->clearState();
}

#include "PresetPreloader.moc"
