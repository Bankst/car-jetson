#pragma once

#include <QQuickFramebufferObject>
#include <QString>
#include <QStringList>
#include <memory>

class AudioCapture;
class AudioRingConsumer;
class Favorites;
class VisualizerRenderer;

class Visualizer : public QQuickFramebufferObject {
    Q_OBJECT
    Q_PROPERTY(QString presetPath READ presetPath WRITE setPresetPath NOTIFY presetPathChanged)
    Q_PROPERTY(QString currentPreset READ currentPreset NOTIFY currentPresetChanged)
    Q_PROPERTY(QString currentFilename READ currentFilename NOTIFY currentPresetChanged)
    Q_PROPERTY(bool currentIsFavorite READ currentIsFavorite NOTIFY currentIsFavoriteChanged)
    Q_PROPERTY(bool favoritesMode READ favoritesMode WRITE setFavoritesMode NOTIFY favoritesModeChanged)
    Q_PROPERTY(int  favoritesCount READ favoritesCount NOTIFY favoritesCountChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY navStateChanged)
    Q_PROPERTY(bool canPrev READ canPrev NOTIFY navStateChanged)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
    Q_PROPERTY(float sensitivity READ sensitivity WRITE setSensitivity NOTIFY sensitivityChanged)
    Q_PROPERTY(int presetDuration READ presetDuration WRITE setPresetDuration NOTIFY presetDurationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit Visualizer(QQuickItem* parent = nullptr);
    ~Visualizer() override;

    Renderer* createRenderer() const override;

    QString presetPath() const { return m_presetPath; }
    void setPresetPath(const QString& p);
    QString currentPreset() const { return m_currentPreset; }
    QString currentFilename() const { return m_currentFilename; }
    bool    currentIsFavorite() const;
    bool    favoritesMode() const { return m_favoritesMode; }
    void    setFavoritesMode(bool on);
    int     favoritesCount() const;
    bool    canNext() const;
    bool    canPrev() const;
    bool    locked() const { return m_locked; }
    void    setLocked(bool on);
    Q_INVOKABLE bool toggleLock();
    float   sensitivity() const { return m_sensitivity; }
    void    setSensitivity(float v);
    int     presetDuration() const { return m_presetDuration; }
    void    setPresetDuration(int secs);
    bool    active() const { return m_active; }
    void    setActive(bool v) { if (v != m_active) { m_active = v; emit activeChanged(); if (v) update(); } }

    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    Q_INVOKABLE void shuffle(bool on);
    Q_INVOKABLE bool toggleFavorite();   // returns new state
    // Flush current preset to disk. Idempotent; safe to call anytime.
    Q_INVOKABLE void persistState();

    // Renderer ctor reads this once to restore last-played preset.
    QString lastPresetHint() const;

    // Invoked from renderer thread via queued connection.
    void setCurrentPresetFromRenderer(const QString& name, const QString& filename);
    void setNavStateFromRenderer(int playlistSize, int currentIndex, bool shuffle);

signals:
    void presetPathChanged();
    void currentPresetChanged();
    void currentIsFavoriteChanged();
    void favoritesModeChanged();
    void favoritesCountChanged();
    void navStateChanged();
    void lockedChanged();
    void sensitivityChanged();
    void presetDurationChanged();
    void activeChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    friend class VisualizerRenderer;
    QString m_presetPath;
    QString m_currentPreset;
    QString m_currentFilename;
    bool    m_favoritesMode = false;
    int     m_playlistSize  = 0;
    int     m_currentIndex  = -1;
    bool    m_shuffleOn     = false;
    bool    m_locked        = false;
    float   m_sensitivity   = 1.0f;
    int     m_presetDuration = 30;
    Favorites* m_favorites = nullptr;
    AudioCapture* m_audio = nullptr;  // borrowed singleton, owned by qApp
    std::unique_ptr<AudioRingConsumer> m_audioRing;

    // Command queue consumed in renderer's synchronize().
    enum class Cmd { None, Next, Prev, ShuffleOn, ShuffleOff, ReloadPlaylist, SetFavoritesMode, LockOn, LockOff, SetSensitivity, SetPresetDuration };
    std::vector<Cmd> m_pendingCmds;
    QStringList m_pendingFavoritesList;  // payload for SetFavoritesMode
    bool        m_pendingFavoritesOn = false;
    float       m_pendingSensitivity = 1.0f;
    int         m_pendingPresetDuration = 30;
    bool        m_active = true;
};
