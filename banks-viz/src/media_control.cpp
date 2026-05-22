#include "media_control.h"
#include <cstdio>
#include <cstring>

bool MediaControl::init() {
    int r = sd_bus_open_system(&bus);
    if (r < 0) {
        fprintf(stderr, "sd_bus_open_system: %s\n", strerror(-r));
        return false;
    }

    r = sd_bus_match_signal(bus, &match_slot,
        "org.bluez",
        nullptr,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged",
        on_properties_changed, this);
    if (r < 0)
        fprintf(stderr, "sd_bus_match_signal: %s\n", strerror(-r));

    discover_player();
    return true;
}

void MediaControl::shutdown() {
    if (match_slot) { sd_bus_slot_unref(match_slot); match_slot = nullptr; }
    if (bus) { sd_bus_unref(bus); bus = nullptr; }
}

void MediaControl::poll() {
    if (!bus) return;
    while (sd_bus_process(bus, nullptr) > 0)
        ;
}

void MediaControl::discover_player() {
    if (!bus) return;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus,
        "org.bluez", "/",
        "org.freedesktop.DBus.ObjectManager", "GetManagedObjects",
        &error, &reply, "");
    if (r < 0) {
        sd_bus_error_free(&error);
        return;
    }

    r = sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
    if (r < 0) goto done;

    while (sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}") > 0) {
        const char* path = nullptr;
        sd_bus_message_read(reply, "o", &path);

        sd_bus_message_enter_container(reply, 'a', "{sa{sv}}");
        while (sd_bus_message_enter_container(reply, 'e', "sa{sv}") > 0) {
            const char* iface = nullptr;
            sd_bus_message_read(reply, "s", &iface);

            if (iface && strcmp(iface, "org.bluez.MediaPlayer1") == 0 && path)
                player_path = path;

            sd_bus_message_skip(reply, "a{sv}");
            sd_bus_message_exit_container(reply);
        }
        sd_bus_message_exit_container(reply);
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_exit_container(reply);

    if (!player_path.empty()) {
        fprintf(stderr, "Found BT player: %s\n", player_path.c_str());
        read_all_properties();
    }

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
}

void MediaControl::read_all_properties() {
    if (!bus || player_path.empty()) return;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(bus,
        "org.bluez", player_path.c_str(),
        "org.freedesktop.DBus.Properties", "GetAll",
        &error, &reply, "s", "org.bluez.MediaPlayer1");
    if (r < 0) goto done;

    r = sd_bus_message_enter_container(reply, 'a', "{sv}");
    if (r < 0) goto done;

    while (sd_bus_message_enter_container(reply, 'e', "sv") > 0) {
        const char* prop = nullptr;
        sd_bus_message_read(reply, "s", &prop);

        if (prop && strcmp(prop, "Status") == 0) {
            const char* val = nullptr;
            sd_bus_message_enter_container(reply, 'v', "s");
            sd_bus_message_read(reply, "s", &val);
            if (val) info.status = val;
            sd_bus_message_exit_container(reply);
        } else if (prop && strcmp(prop, "Position") == 0) {
            uint32_t val = 0;
            sd_bus_message_enter_container(reply, 'v', "u");
            sd_bus_message_read(reply, "u", &val);
            info.position_ms = val;
            sd_bus_message_exit_container(reply);
        } else if (prop && strcmp(prop, "Track") == 0) {
            sd_bus_message_enter_container(reply, 'v', "a{sv}");
            parse_track_dict(reply);
            sd_bus_message_exit_container(reply);
        } else {
            sd_bus_message_skip(reply, "v");
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_exit_container(reply);

done:
    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
}

void MediaControl::parse_track_dict(sd_bus_message* m) {
    int r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) return;

    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* key = nullptr;
        sd_bus_message_read(m, "s", &key);

        if (key && strcmp(key, "Title") == 0) {
            const char* val = nullptr;
            sd_bus_message_enter_container(m, 'v', "s");
            sd_bus_message_read(m, "s", &val);
            if (val) info.title = val;
            sd_bus_message_exit_container(m);
        } else if (key && strcmp(key, "Artist") == 0) {
            const char* val = nullptr;
            sd_bus_message_enter_container(m, 'v', "s");
            sd_bus_message_read(m, "s", &val);
            if (val) info.artist = val;
            sd_bus_message_exit_container(m);
        } else if (key && strcmp(key, "Album") == 0) {
            const char* val = nullptr;
            sd_bus_message_enter_container(m, 'v', "s");
            sd_bus_message_read(m, "s", &val);
            if (val) info.album = val;
            sd_bus_message_exit_container(m);
        } else if (key && strcmp(key, "Duration") == 0) {
            uint32_t val = 0;
            sd_bus_message_enter_container(m, 'v', "u");
            sd_bus_message_read(m, "u", &val);
            info.duration_ms = val;
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);
}

int MediaControl::on_properties_changed(sd_bus_message* m, void* userdata, sd_bus_error*) {
    auto* mc = static_cast<MediaControl*>(userdata);

    const char* iface = nullptr;
    int r = sd_bus_message_read(m, "s", &iface);
    if (r < 0 || !iface) return 0;
    if (strcmp(iface, "org.bluez.MediaPlayer1") != 0) return 0;

    const char* path = sd_bus_message_get_path(m);
    if (path) mc->player_path = path;

    r = sd_bus_message_enter_container(m, 'a', "{sv}");
    if (r < 0) return 0;

    while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
        const char* prop = nullptr;
        sd_bus_message_read(m, "s", &prop);

        if (prop && strcmp(prop, "Status") == 0) {
            const char* val = nullptr;
            sd_bus_message_enter_container(m, 'v', "s");
            sd_bus_message_read(m, "s", &val);
            if (val) mc->info.status = val;
            sd_bus_message_exit_container(m);
        } else if (prop && strcmp(prop, "Position") == 0) {
            uint32_t val = 0;
            sd_bus_message_enter_container(m, 'v', "u");
            sd_bus_message_read(m, "u", &val);
            mc->info.position_ms = val;
            sd_bus_message_exit_container(m);
        } else if (prop && strcmp(prop, "Track") == 0) {
            sd_bus_message_enter_container(m, 'v', "a{sv}");
            mc->parse_track_dict(m);
            sd_bus_message_exit_container(m);
        } else {
            sd_bus_message_skip(m, "v");
        }
        sd_bus_message_exit_container(m);
    }
    sd_bus_message_exit_container(m);

    return 0;
}

void MediaControl::send_command(const char* method) {
    if (!bus || player_path.empty()) return;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    sd_bus_call_method(bus,
        "org.bluez", player_path.c_str(),
        "org.bluez.MediaPlayer1", method,
        &error, &reply, "");

    sd_bus_message_unref(reply);
    sd_bus_error_free(&error);
}

void MediaControl::play() { send_command("Play"); }
void MediaControl::pause() { send_command("Pause"); }
void MediaControl::next() { send_command("Next"); }
void MediaControl::previous() { send_command("Previous"); }
