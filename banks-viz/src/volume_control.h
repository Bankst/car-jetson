#pragma once

#include <chrono>

struct VolumeControl {
    void poll();

    float volume() const { return vol; }
    bool muted() const { return is_muted; }

    void set_volume(float v);
    void adjust(float delta);
    void toggle_mute();

private:
    float vol = 0.5f;
    bool is_muted = false;
    std::chrono::steady_clock::time_point last_poll{};

    void run_wpctl(const char* cmd);
};
