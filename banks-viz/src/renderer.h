#pragma once

#include "projectm_wrapper.h"
#include <GLES3/gl3.h>
#include <string>

struct Renderer {
    bool init(int width, int height);
    void shutdown();
    void render();

    bool initialized() const { return (bool)pm; }
    projectm_handle pm_handle() const { return pm.handle(); }
    GLuint texture() const { return tex; }
    int width() const { return viz_w; }
    int height() const { return viz_h; }

    void load_presets(const char* path);
    void next_preset();
    void prev_preset();
    void random_preset();
    void toggle_lock();
    void toggle_aspect_correction();
    void adjust_beat_sensitivity(float delta);
    size_t preset_count() const;
    std::string preset_name() const;
    bool preset_locked() const;

private:
    ProjectM pm;
    GLuint fbo = 0, tex = 0, depth_rb = 0;
    int viz_w = 0, viz_h = 0;

#ifdef HAS_PROJECTM_PLAYLIST
    ProjectMPlaylist playlist;
#endif

    void create_fbo(int w, int h);
    void destroy_fbo();
};
