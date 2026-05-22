#include "gui.h"
#include "renderer.h"
#include "media_control.h"
#include "volume_control.h"
#include "imgui.h"
#include <cstdio>
#include <cstdint>

static void format_time(uint32_t ms, char* buf, size_t len) {
    uint32_t secs = ms / 1000;
    uint32_t mins = secs / 60;
    secs %= 60;
    snprintf(buf, len, "%u:%02u", mins, secs);
}

void Gui::render(Renderer& renderer, MediaControl& media, VolumeControl& volume,
                 int, int) {
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar);

    float panel_width = 340.0f;
    float viz_width = io.DisplaySize.x - panel_width - ImGui::GetStyle().ItemSpacing.x;
    float content_height = io.DisplaySize.y - ImGui::GetStyle().WindowPadding.y * 2;

    // --- Visualization ---
    ImGui::BeginChild("viz", ImVec2(viz_width, content_height));
    if (renderer.initialized()) {
        float tex_w = viz_width;
        float tex_h = content_height;
        float aspect = (float)renderer.width() / (float)renderer.height();
        if (tex_w / tex_h > aspect)
            tex_w = tex_h * aspect;
        else
            tex_h = tex_w / aspect;

        float pad_x = (viz_width - tex_w) * 0.5f;
        float pad_y = (content_height - tex_h) * 0.5f;
        if (pad_x > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
        if (pad_y > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad_y);

        ImGui::Image((ImTextureID)(uintptr_t)renderer.texture(),
                     ImVec2(tex_w, tex_h), ImVec2(0, 1), ImVec2(1, 0));
    } else {
        ImGui::TextDisabled("Visualization unavailable");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // --- Controls ---
    ImGui::BeginChild("controls", ImVec2(panel_width, content_height));

    ImGui::TextUnformatted("NOW PLAYING");
    ImGui::Separator();
    ImGui::Spacing();

    if (media.has_player()) {
        const auto& t = media.track();

        if (!t.title.empty())
            ImGui::TextWrapped("%s", t.title.c_str());
        else
            ImGui::TextDisabled("Unknown Track");

        if (!t.artist.empty())
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", t.artist.c_str());

        if (!t.album.empty())
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", t.album.c_str());

        ImGui::Spacing();

        float progress = 0.0f;
        if (t.duration_ms > 0)
            progress = (float)t.position_ms / (float)t.duration_ms;

        char pos_str[16], dur_str[16], overlay[48];
        format_time(t.position_ms, pos_str, sizeof(pos_str));
        format_time(t.duration_ms, dur_str, sizeof(dur_str));
        snprintf(overlay, sizeof(overlay), "%s / %s", pos_str, dur_str);
        ImGui::ProgressBar(progress, ImVec2(-1, 0), overlay);

        ImGui::Spacing();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, 12));
        float avail = ImGui::GetContentRegionAvail().x;
        float btn_w = (avail - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;

        if (ImGui::Button("Prev", ImVec2(btn_w, 0)))
            media.previous();
        ImGui::SameLine();

        bool playing = (t.status == "playing");
        if (ImGui::Button(playing ? "Pause" : "Play", ImVec2(btn_w, 0))) {
            if (playing) media.pause(); else media.play();
        }
        ImGui::SameLine();

        if (ImGui::Button("Next", ImVec2(btn_w, 0)))
            media.next();

        ImGui::PopStyleVar();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "%s", t.status.c_str());
    } else {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.3f, 1.0f),
            "No Bluetooth player connected");
        ImGui::Spacing();
        ImGui::TextWrapped("Pair a phone and play music to see controls here.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // --- Volume ---
    ImGui::TextUnformatted("VOLUME");
    ImGui::Spacing();

    float vol_pct = volume.volume() * 100.0f;
    if (ImGui::SliderFloat("##vol", &vol_pct, 0.0f, 100.0f, "%.0f%%"))
        volume.set_volume(vol_pct / 100.0f);

    if (volume.muted())
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "MUTED");

    if (ImGui::Button(volume.muted() ? "Unmute" : "Mute", ImVec2(-1, 0)))
        volume.toggle_mute();

    // --- Presets ---
    if (renderer.initialized() && renderer.preset_count() > 0) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("PRESET");
        ImGui::Spacing();

        std::string name = renderer.preset_name();
        if (!name.empty())
            ImGui::TextWrapped("%s", name.c_str());

        ImGui::Text("%zu presets", renderer.preset_count());

        ImGui::Spacing();
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
        if (ImGui::Button("< Prev##preset", ImVec2(half, 0)))
            renderer.prev_preset();
        ImGui::SameLine();
        if (ImGui::Button("Next >##preset", ImVec2(half, 0)))
            renderer.next_preset();
    }

    ImGui::EndChild();
    ImGui::End();
}
