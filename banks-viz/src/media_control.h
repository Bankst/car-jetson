#pragma once

#include <systemd/sd-bus.h>
#include <string>
#include <cstdint>

struct TrackInfo {
    std::string title;
    std::string artist;
    std::string album;
    std::string status;
    uint32_t position_ms = 0;
    uint32_t duration_ms = 0;
};

struct MediaControl {
    bool init();
    void shutdown();
    void poll();

    const TrackInfo& track() const { return info; }
    bool has_player() const { return !player_path.empty(); }

    void play();
    void pause();
    void next();
    void previous();

private:
    sd_bus* bus = nullptr;
    sd_bus_slot* match_slot = nullptr;
    std::string player_path;
    TrackInfo info;

    void discover_player();
    void read_all_properties();
    void parse_track_dict(sd_bus_message* m);
    void send_command(const char* method);

    static int on_properties_changed(sd_bus_message* m, void* userdata, sd_bus_error* err);
};
