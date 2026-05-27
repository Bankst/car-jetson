#include "Visualizer.h"
#include "AudioCapture.h"
#include "AudioRingConsumer.h"
#include "Favorites.h"
#include "Log.h"

#include <QCoreApplication>
#include <QVariant>

#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QMetaObject>
#include <projectM-4/projectM.h>
#include <projectM-4/parameters.h>
#include <projectM-4/render_opengl.h>
#include <projectM-4/playlist.h>
#include <projectM-4/playlist_callbacks.h>

// --- Renderer lives on Qt render thread. Owns projectM + FBO. ---
class VisualizerRenderer : public QQuickFramebufferObject::Renderer {
public:
    explicit VisualizerRenderer(const Visualizer* item)
        : m_presetPath(item->presetPath().toUtf8())
        , m_lastPresetHint(item->lastPresetHint())
        , m_item(const_cast<Visualizer*>(item)) {
        qCInfo(logViz) << "Renderer ctor, presetPath=" << m_presetPath;
    }

    ~VisualizerRenderer() override {
        qCInfo(logViz) << "Renderer dtor";
        if (m_playlist) projectm_playlist_destroy(m_playlist);
        if (m_pm)       projectm_destroy(m_pm);
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        fmt.setSamples(0);

        ensureProjectM(size.width(), size.height());
        loadPresetsOnce();
        return new QOpenGLFramebufferObject(size, fmt);
    }

    void synchronize(QQuickFramebufferObject* fbo) override {
        auto* item = static_cast<Visualizer*>(fbo);

        for (auto cmd : item->m_pendingCmds) {
            if (!m_playlist) continue;
            switch (cmd) {
                case Visualizer::Cmd::Next:
                    projectm_playlist_play_next(m_playlist, true); break;
                case Visualizer::Cmd::Prev:
                    projectm_playlist_play_previous(m_playlist, true); break;
                case Visualizer::Cmd::ShuffleOn:  projectm_playlist_set_shuffle(m_playlist, true);  break;
                case Visualizer::Cmd::ShuffleOff: projectm_playlist_set_shuffle(m_playlist, false); break;
                case Visualizer::Cmd::LockOn:  projectm_set_preset_locked(m_pm, true);  qCInfo(logViz) << "preset locked";   break;
                case Visualizer::Cmd::LockOff: projectm_set_preset_locked(m_pm, false); qCInfo(logViz) << "preset unlocked"; break;
                case Visualizer::Cmd::SetSensitivity:
                    projectm_set_beat_sensitivity(m_pm, item->m_pendingSensitivity);
                    qCInfo(logViz) << "beat sensitivity =" << item->m_pendingSensitivity;
                    break;
                case Visualizer::Cmd::SetPresetDuration:
                    projectm_set_preset_duration(m_pm, static_cast<double>(item->m_pendingPresetDuration));
                    qCInfo(logViz) << "preset duration =" << item->m_pendingPresetDuration << "s";
                    break;
                case Visualizer::Cmd::SetFavoritesMode: {
                    projectm_playlist_clear(m_playlist);
                    if (item->m_pendingFavoritesOn) {
                        for (const QString& f : item->m_pendingFavoritesList) {
                            projectm_playlist_add_preset(m_playlist, f.toUtf8().constData(), false);
                        }
                        qCInfo(logViz) << "switched to favorites:" << item->m_pendingFavoritesList.size();
                    } else {
                        projectm_playlist_add_path(m_playlist, m_presetPath.constData(), true, false);
                        qCInfo(logViz) << "switched to full library";
                    }
                    projectm_playlist_play_next(m_playlist, true);
                    pushNavState();
                    break;
                }
                default: break;
            }
        }
        item->m_pendingCmds.clear();
        item->m_pendingFavoritesList.clear();

        m_audioRing = item->m_audioRing.get();
    }

    void render() override {
        if (!m_pm) {
            if (!m_warnedNoPm) { qCWarning(logViz) << "render(): no projectM handle"; m_warnedNoPm = true; }
            return;
        }

        if (m_audioRing) {
            float samples[512];
            size_t got = m_audioRing->pop(samples, 512);
            if (got > 0) projectm_pcm_add_float(m_pm, samples, got, PROJECTM_MONO);
        }

        auto* f = QOpenGLContext::currentContext()->functions();
        GLint qtFbo = 0;
        f->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &qtFbo);
        projectm_opengl_render_frame_fbo(m_pm, static_cast<uint32_t>(qtFbo));

        if (m_item && m_item->active()) update();
    }

private:
    void loadPresetsOnce() {
        if (m_presetPathLoaded || m_presetPath.isEmpty() || !m_playlist) return;
        uint32_t added = projectm_playlist_add_path(m_playlist, m_presetPath.constData(), true, false);
        if (added == 0) {
            qCWarning(logViz) << "No presets loaded from" << m_presetPath
                              << "(exists:" << QDir(QString::fromUtf8(m_presetPath)).exists() << ")";
        } else {
            qCInfo(logViz) << "Loaded" << added << "presets from" << m_presetPath;
        }
        projectm_playlist_set_shuffle(m_playlist, false);
        projectm_playlist_set_preset_switched_event_callback(
            m_playlist, &VisualizerRenderer::onPresetSwitched, this);

        // Restore last-played preset if persisted and still present in playlist.
        int restored = -1;
        if (!m_lastPresetHint.isEmpty()) {
            const QByteArray want = m_lastPresetHint.toUtf8();
            const uint32_t total = projectm_playlist_size(m_playlist);
            for (uint32_t i = 0; i < total; ++i) {
                char* fn = projectm_playlist_item(m_playlist, i);
                bool match = fn && (want == fn);
                if (fn) projectm_playlist_free_string(fn);
                if (match) { restored = static_cast<int>(i); break; }
            }
        }
        if (restored >= 0) {
            projectm_playlist_set_position(m_playlist, static_cast<uint32_t>(restored), true);
            qCInfo(logViz) << "restored last preset at index" << restored;
        } else {
            projectm_playlist_play_next(m_playlist, true);
        }
        m_presetPathLoaded = true;
    }

    static void onPresetSwitched(bool /*hardCut*/, unsigned int index, void* user) {
        auto* self = static_cast<VisualizerRenderer*>(user);
        char* fn = projectm_playlist_item(self->m_playlist, index);
        QString name, fullPath;
        if (fn) {
            fullPath = QString::fromUtf8(fn);
            name = QFileInfo(fullPath).completeBaseName();
            projectm_playlist_free_string(fn);
        }
        const int size = static_cast<int>(projectm_playlist_size(self->m_playlist));
        const bool shuf = projectm_playlist_get_shuffle(self->m_playlist);
        qCInfo(logViz).noquote() << "preset[" << (static_cast<int>(index) + 1) << "/" << size << "]:" << name;

        QPointer<Visualizer> item(self->m_item);
        const int idx = static_cast<int>(index);
        QMetaObject::invokeMethod(item.data(), [item, name, fullPath, size, idx, shuf] {
            if (!item) return;
            item->setCurrentPresetFromRenderer(name, fullPath);
            item->setNavStateFromRenderer(size, idx, shuf);
        }, Qt::QueuedConnection);
    }

    void pushNavState() {
        if (!m_playlist) return;
        const int size = static_cast<int>(projectm_playlist_size(m_playlist));
        const int idx  = static_cast<int>(projectm_playlist_get_position(m_playlist));
        const bool shuf = projectm_playlist_get_shuffle(m_playlist);
        QPointer<Visualizer> item(m_item);
        QMetaObject::invokeMethod(item.data(), [item, size, idx, shuf] {
            if (item) item->setNavStateFromRenderer(size, idx, shuf);
        }, Qt::QueuedConnection);
    }

    void ensureProjectM(int w, int h) {
        if (m_pm) { projectm_set_window_size(m_pm, w, h); return; }
        qCInfo(logViz) << "projectm_create" << w << "x" << h;
        m_pm = projectm_create();
        if (!m_pm) { qCCritical(logViz) << "projectm_create FAILED"; return; }
        projectm_set_window_size(m_pm, w, h);
        projectm_set_mesh_size(m_pm, 64, 36);
        projectm_set_fps(m_pm, 60);
        projectm_set_beat_sensitivity(m_pm, m_item->m_sensitivity);
        projectm_set_preset_duration(m_pm, static_cast<double>(m_item->m_presetDuration));
        m_playlist = projectm_playlist_create(m_pm);
        qCInfo(logViz) << "  pm=" << (void*)m_pm << "playlist=" << (void*)m_playlist;
    }

    QByteArray m_presetPath;
    QString    m_lastPresetHint;
    Visualizer* m_item = nullptr;
    bool       m_presetPathLoaded = false;
    bool       m_warnedNoPm = false;
    projectm_handle          m_pm = nullptr;
    projectm_playlist_handle m_playlist = nullptr;
    AudioRingConsumer* m_audioRing = nullptr;
};

// --- Visualizer (GUI thread side) ---
Visualizer::Visualizer(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    setMirrorVertically(true);
    const QByteArray env = qgetenv("BANKS_FRONTEND_PRESETS");
    m_presetPath = env.isEmpty() ? QString::fromUtf8(BANKS_FRONTEND_PRESET_PATH)
                                 : QString::fromUtf8(env);
    qCInfo(logViz) << "Visualizer ctor, presetPath=" << m_presetPath;

    m_favorites = new Favorites(this);
    connect(m_favorites, &Favorites::changed, this, [this]{
        emit favoritesCountChanged();
        emit currentIsFavoriteChanged();
    });
}
Visualizer::~Visualizer() { qCInfo(logViz) << "Visualizer dtor"; }

QQuickFramebufferObject::Renderer* Visualizer::createRenderer() const {
    return new VisualizerRenderer(this);
}

bool Visualizer::currentIsFavorite() const {
    return m_favorites && !m_currentFilename.isEmpty() && m_favorites->contains(m_currentFilename);
}

int Visualizer::favoritesCount() const {
    return m_favorites ? m_favorites->count() : 0;
}

QString Visualizer::lastPresetHint() const {
    return m_favorites ? m_favorites->lastPreset() : QString{};
}

bool Visualizer::toggleFavorite() {
    if (!m_favorites || m_currentFilename.isEmpty()) return false;
    const bool on = m_favorites->toggle(m_currentFilename);
    emit currentIsFavoriteChanged();
    // If favorites mode is active and we just removed the current preset, leave it playing;
    // user can hit Next to move on.
    return on;
}

void Visualizer::setFavoritesMode(bool on) {
    if (on == m_favoritesMode) return;
    if (on && (!m_favorites || m_favorites->count() == 0)) {
        qCWarning(logViz) << "favorites mode requested but list empty — staying on full library";
        return;
    }
    m_favoritesMode = on;
    m_pendingFavoritesOn = on;
    m_pendingFavoritesList = on ? m_favorites->list() : QStringList{};
    m_pendingCmds.push_back(Cmd::SetFavoritesMode);
    emit favoritesModeChanged();
    update();
}

void Visualizer::setPresetPath(const QString& p) {
    if (p == m_presetPath) return;
    m_presetPath = p;
    m_pendingCmds.push_back(Cmd::ReloadPlaylist);
    emit presetPathChanged();
    update();
}

void Visualizer::setCurrentPresetFromRenderer(const QString& name, const QString& filename) {
    if (name == m_currentPreset && filename == m_currentFilename) return;
    m_currentPreset   = name;
    m_currentFilename = filename;
    if (m_favorites && !filename.isEmpty()) m_favorites->setLastPreset(filename);
    emit currentPresetChanged();
    emit currentIsFavoriteChanged();
}

void Visualizer::setNavStateFromRenderer(int playlistSize, int currentIndex, bool shuffle) {
    if (playlistSize == m_playlistSize && currentIndex == m_currentIndex && shuffle == m_shuffleOn) return;
    m_playlistSize = playlistSize;
    m_currentIndex = currentIndex;
    m_shuffleOn    = shuffle;
    emit navStateChanged();
}

bool Visualizer::canNext() const {
    if (m_playlistSize <= 1) return false;
    if (m_shuffleOn)         return true;          // shuffle always has another
    return m_currentIndex >= 0 && m_currentIndex < m_playlistSize - 1;
}

bool Visualizer::canPrev() const {
    if (m_playlistSize <= 1) return false;
    if (m_shuffleOn)         return true;          // history-driven
    return m_currentIndex > 0;
}

void Visualizer::next() {
    if (!canNext()) return;
    m_pendingCmds.push_back(Cmd::Next); update();
}
void Visualizer::prev() {
    if (!canPrev()) return;
    m_pendingCmds.push_back(Cmd::Prev); update();
}
void Visualizer::setLocked(bool on) {
    if (on == m_locked) return;
    m_locked = on;
    m_pendingCmds.push_back(on ? Cmd::LockOn : Cmd::LockOff);
    emit lockedChanged();
    update();
}

bool Visualizer::toggleLock() {
    setLocked(!m_locked);
    return m_locked;
}

void Visualizer::setSensitivity(float v) {
    v = qBound(0.0f, v, 2.0f);
    if (qFuzzyCompare(v, m_sensitivity)) return;
    m_sensitivity = v;
    m_pendingSensitivity = v;
    m_pendingCmds.push_back(Cmd::SetSensitivity);
    emit sensitivityChanged();
    update();
}

void Visualizer::setPresetDuration(int secs) {
    secs = qBound(5, secs, 120);
    if (secs == m_presetDuration) return;
    m_presetDuration = secs;
    m_pendingPresetDuration = secs;
    m_pendingCmds.push_back(Cmd::SetPresetDuration);
    emit presetDurationChanged();
    update();
}

void Visualizer::shuffle(bool on) {
    m_shuffleOn = on;
    m_pendingCmds.push_back(on ? Cmd::ShuffleOn : Cmd::ShuffleOff);
    emit navStateChanged();
    update();
}

void Visualizer::itemChange(ItemChange change, const ItemChangeData& data) {
    if (change == ItemSceneChange) {
        if (data.window) {
            qCInfo(logViz) << "Visualizer entered scene, attaching to audio singleton";
            m_audio = reinterpret_cast<AudioCapture*>(
                qApp->property("audioCapturePtr").value<quintptr>());
            if (m_audio) {
                m_audioRing = std::make_unique<AudioRingConsumer>(16384);
                m_audio->addConsumer(m_audioRing.get());
            } else {
                qCWarning(logViz) << "no AudioCapture singleton attached to qApp";
            }
        } else {
            qCInfo(logViz) << "Visualizer left scene, detaching from audio singleton";
            if (m_audio && m_audioRing) m_audio->removeConsumer(m_audioRing.get());
            m_audioRing.reset();
            m_audio = nullptr;
        }
    }
    QQuickFramebufferObject::itemChange(change, data);
}
