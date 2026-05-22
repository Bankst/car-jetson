#include "renderer.h"
#include <cstdio>

bool Renderer::init(int width, int height) {
    if (!pm) {
        fprintf(stderr, "projectm_create failed\n");
        return false;
    }

    pm.set_mesh_size(48, 32);
    pm.set_fps(60);
    pm.set_preset_duration(30.0);
    pm.set_soft_cut_duration(3.0);
    pm.set_hard_cut_enabled(true);
    pm.set_beat_sensitivity(1.0f);
    pm.set_window_size(width, height);

    viz_w = width;
    viz_h = height;
    create_fbo(width, height);

    return true;
}

void Renderer::shutdown() {
    destroy_fbo();
}

void Renderer::render() {
    if (!pm) return;
    pm.render_to_fbo(fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::create_fbo(int w, int h) {
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);
    glGenRenderbuffers(1, &depth_rb);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindRenderbuffer(GL_RENDERBUFFER, depth_rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rb);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "FBO incomplete: 0x%x\n", status);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Renderer::destroy_fbo() {
    if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if (tex) { glDeleteTextures(1, &tex); tex = 0; }
    if (depth_rb) { glDeleteRenderbuffers(1, &depth_rb); depth_rb = 0; }
}

void Renderer::load_presets(const char* path) {
#ifdef HAS_PROJECTM_PLAYLIST
    if (!pm) return;
    playlist = ProjectMPlaylist(pm.handle());
    if (!playlist) return;

    uint32_t added = playlist.add_path(path, true, false);
    if (added == 0) {
        fprintf(stderr, "No presets found in %s\n", path);
        return;
    }
    fprintf(stderr, "Loaded %u presets from %s\n", added, path);
    playlist.set_shuffle(true);
    playlist.play_next(true);
#else
    (void)path;
#endif
}

void Renderer::next_preset() {
#ifdef HAS_PROJECTM_PLAYLIST
    if (playlist) playlist.play_next(false);
#endif
}

void Renderer::prev_preset() {
#ifdef HAS_PROJECTM_PLAYLIST
    if (playlist) playlist.play_prev(false);
#endif
}

void Renderer::random_preset() {
#ifdef HAS_PROJECTM_PLAYLIST
    if (playlist) {
        bool was = playlist.shuffle();
        playlist.set_shuffle(true);
        playlist.play_next(true);
        playlist.set_shuffle(was);
    }
#endif
}

size_t Renderer::preset_count() const {
#ifdef HAS_PROJECTM_PLAYLIST
    if (playlist) return playlist.size();
#endif
    return 0;
}

std::string Renderer::preset_name() const {
#ifdef HAS_PROJECTM_PLAYLIST
    if (playlist) return playlist.current_name();
#endif
    return "";
}

void Renderer::toggle_lock() {
    if (pm) pm.set_preset_locked(!pm.preset_locked());
}

void Renderer::toggle_aspect_correction() {
    if (pm) pm.set_aspect_correction(!pm.aspect_correction());
}

void Renderer::adjust_beat_sensitivity(float delta) {
    if (pm) pm.set_beat_sensitivity(pm.beat_sensitivity() + delta);
}

bool Renderer::preset_locked() const {
    if (pm) return pm.preset_locked();
    return false;
}
