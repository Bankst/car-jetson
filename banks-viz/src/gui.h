#pragma once

struct Renderer;
struct MediaControl;
struct VolumeControl;

struct Gui {
    void render(Renderer& renderer, MediaControl& media, VolumeControl& volume,
                int window_w, int window_h);
};
