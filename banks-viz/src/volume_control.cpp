#include "volume_control.h"
#include <cstdio>
#include <cstring>

void VolumeControl::poll() {
    auto now = std::chrono::steady_clock::now();
    if (now - last_poll < std::chrono::seconds(1))
        return;
    last_poll = now;

    FILE* fp = popen("wpctl get-volume @DEFAULT_AUDIO_SINK@ 2>/dev/null", "r");
    if (!fp) return;

    char buf[128];
    if (fgets(buf, sizeof(buf), fp)) {
        float v = 0.0f;
        if (sscanf(buf, "Volume: %f", &v) == 1)
            vol = v;
        is_muted = strstr(buf, "[MUTED]") != nullptr;
    }
    pclose(fp);
}

void VolumeControl::set_volume(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    char cmd[128];
    snprintf(cmd, sizeof(cmd),
        "wpctl set-volume @DEFAULT_AUDIO_SINK@ %.2f 2>/dev/null", v);
    run_wpctl(cmd);
    vol = v;
    last_poll = {};
}

void VolumeControl::adjust(float delta) {
    char cmd[128];
    if (delta >= 0)
        snprintf(cmd, sizeof(cmd),
            "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ %.2f+ 2>/dev/null", delta);
    else
        snprintf(cmd, sizeof(cmd),
            "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ %.2f- 2>/dev/null", -delta);
    run_wpctl(cmd);
    last_poll = {};
}

void VolumeControl::toggle_mute() {
    run_wpctl("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle 2>/dev/null");
    is_muted = !is_muted;
    last_poll = {};
}

void VolumeControl::run_wpctl(const char* cmd) {
    FILE* fp = popen(cmd, "r");
    if (fp) pclose(fp);
}
