#pragma once

#include <QObject>
#include <QThread>
#include <QStringList>

class QOpenGLContext;
class QOffscreenSurface;

// Background shader pre-compiler for projectM presets.
//
// Creates a hidden projectM instance on a shared GL context running on a
// dedicated thread. Loads upcoming presets and renders one frame each at
// 64x64 to force GLSL compilation into the GPU driver cache. When the
// primary Visualizer later loads those presets, the driver returns the
// cached program instantly -- eliminating 100-500ms stalls on Tegra GLES.
//
// Threading: the hidden projectm_handle is only touched on the worker
// thread. The shared QOpenGLContext handles GL object sharing between
// contexts automatically.
class PresetPreloader : public QObject {
    Q_OBJECT
public:
    // shareContext must be the render thread's QOpenGLContext (current when
    // called). Ownership stays with the caller.
    explicit PresetPreloader(QOpenGLContext* shareContext, QObject* parent = nullptr);
    ~PresetPreloader() override;

    void setLookaheadDepth(int depth);
    int  lookaheadDepth() const;

    // Queue preset paths for background compilation. Duplicates and
    // already-compiled paths are silently skipped.
    void preloadPresets(const QStringList& presetPaths);

    // Cancel pending work and clear the compiled-set.
    void clear();

signals:
    void presetPreloaded(const QString& path);

private:
    class Worker;
    QThread       m_thread;
    Worker*       m_worker = nullptr;  // lives on m_thread
};
