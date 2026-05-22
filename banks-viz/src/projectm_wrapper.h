#pragma once

#include <projectM-4/projectM.h>
#include <string>
#include <cstdint>

#ifdef HAS_PROJECTM_PLAYLIST
#include <projectM-4/playlist.h>
#endif

struct ProjectM {
    ProjectM() : pm_(projectm_create()) {}
    ~ProjectM() { if (pm_) projectm_destroy(pm_); }

    ProjectM(const ProjectM&) = delete;
    ProjectM& operator=(const ProjectM&) = delete;
    ProjectM(ProjectM&& o) noexcept : pm_(o.pm_) { o.pm_ = nullptr; }
    ProjectM& operator=(ProjectM&& o) noexcept {
        if (this != &o) { if (pm_) projectm_destroy(pm_); pm_ = o.pm_; o.pm_ = nullptr; }
        return *this;
    }

    explicit operator bool() const { return pm_ != nullptr; }
    projectm_handle handle() const { return pm_; }

    void set_window_size(size_t w, size_t h)   { projectm_set_window_size(pm_, w, h); }
    void set_mesh_size(size_t w, size_t h)     { projectm_set_mesh_size(pm_, w, h); }
    void set_fps(int32_t fps)                  { projectm_set_fps(pm_, fps); }
    void set_preset_duration(double s)         { projectm_set_preset_duration(pm_, s); }
    void set_soft_cut_duration(double s)       { projectm_set_soft_cut_duration(pm_, s); }
    void set_hard_cut_enabled(bool v)          { projectm_set_hard_cut_enabled(pm_, v); }
    void set_beat_sensitivity(float v)         { projectm_set_beat_sensitivity(pm_, v); }
    float beat_sensitivity() const             { return projectm_get_beat_sensitivity(pm_); }
    void set_aspect_correction(bool v)         { projectm_set_aspect_correction(pm_, v); }
    bool aspect_correction() const             { return projectm_get_aspect_correction(pm_); }
    void set_preset_locked(bool v)             { projectm_set_preset_locked(pm_, v); }
    bool preset_locked() const                 { return projectm_get_preset_locked(pm_); }

    void render_to_fbo(uint32_t fbo)           { projectm_opengl_render_frame_fbo(pm_, fbo); }
    void render()                              { projectm_opengl_render_frame(pm_); }

    void add_pcm(const float* s, unsigned int n, projectm_channels ch) {
        projectm_pcm_add_float(pm_, s, n, ch);
    }

private:
    projectm_handle pm_;
};

#ifdef HAS_PROJECTM_PLAYLIST
struct ProjectMPlaylist {
    ProjectMPlaylist() = default;
    explicit ProjectMPlaylist(projectm_handle pm) : pl_(projectm_playlist_create(pm)) {}
    ~ProjectMPlaylist() { if (pl_) projectm_playlist_destroy(pl_); }

    ProjectMPlaylist(const ProjectMPlaylist&) = delete;
    ProjectMPlaylist& operator=(const ProjectMPlaylist&) = delete;
    ProjectMPlaylist(ProjectMPlaylist&& o) noexcept : pl_(o.pl_) { o.pl_ = nullptr; }
    ProjectMPlaylist& operator=(ProjectMPlaylist&& o) noexcept {
        if (this != &o) { if (pl_) projectm_playlist_destroy(pl_); pl_ = o.pl_; o.pl_ = nullptr; }
        return *this;
    }

    explicit operator bool() const { return pl_ != nullptr; }

    uint32_t add_path(const char* p, bool recurse, bool sort) {
        return projectm_playlist_add_path(pl_, p, recurse, sort);
    }
    void set_shuffle(bool v)       { projectm_playlist_set_shuffle(pl_, v); }
    bool shuffle() const           { return projectm_playlist_get_shuffle(pl_); }
    uint32_t play_next(bool hard)  { return projectm_playlist_play_next(pl_, hard); }
    uint32_t play_prev(bool hard)  { return projectm_playlist_play_previous(pl_, hard); }
    uint32_t size() const          { return projectm_playlist_size(pl_); }
    uint32_t position() const      { return projectm_playlist_get_position(pl_); }

    std::string item_name(uint32_t idx) const {
        char* raw = projectm_playlist_item(pl_, idx);
        if (!raw) return "";
        std::string s(raw);
        projectm_playlist_free_string(raw);
        auto slash = s.rfind('/');
        if (slash != std::string::npos) s = s.substr(slash + 1);
        auto dot = s.rfind('.');
        if (dot != std::string::npos) s = s.substr(0, dot);
        return s;
    }

    std::string current_name() const { return item_name(position()); }

private:
    projectm_playlist_handle pl_ = nullptr;
};
#endif
