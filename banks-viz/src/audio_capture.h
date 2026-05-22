#pragma once

#include <projectM-4/projectM.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

struct AudioCapture {
    bool init(projectm_handle pm_handle);
    void start();
    void stop();
    bool running() const { return is_running; }

    projectm_handle pm = nullptr;
    pw_thread_loop* loop = nullptr;
    pw_context* ctx = nullptr;
    pw_core* core = nullptr;
    pw_stream* stream = nullptr;
    spa_hook listener = {};
    bool is_running = false;
};
