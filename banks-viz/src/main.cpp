#include <SDL.h>
#include <GLES3/gl3.h>
#include <pipewire/pipewire.h>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "renderer.h"
#include "audio_capture.h"
#include "media_control.h"
#include "volume_control.h"
#include "gui.h"

#ifndef DEFAULT_PRESET_PATH
#define DEFAULT_PRESET_PATH "/usr/share/projectMSDL/presets"
#endif

int main(int argc, char* argv[]) {
    struct stat st;
    if (stat("/run/pipewire", &st) == 0)
        setenv("PIPEWIRE_RUNTIME_DIR", "/run/pipewire", 0);

    pw_init(&argc, &argv);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_Window* window = SDL_CreateWindow("Banks Viz",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImFontConfig font_cfg;
    font_cfg.SizePixels = 24.0f;
    io.Fonts->AddFontDefault(&font_cfg);

    ImGui_ImplSDL2_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    Renderer renderer;
    bool renderer_ok = renderer.init(1280, 720);
    if (!renderer_ok)
        fprintf(stderr, "Renderer init failed, continuing without visualization\n");

    AudioCapture audio;
    if (renderer_ok) {
        if (audio.init(renderer.pm_handle()))
            audio.start();
        else
            fprintf(stderr, "Audio capture failed, viz runs without audio\n");
    }

    MediaControl media;
    bool media_ok = media.init();
    if (!media_ok)
        fprintf(stderr, "Media control init failed\n");

    VolumeControl volume;
    volume.poll();

    if (renderer_ok) {
        const char* preset_env = getenv("BANKS_VIZ_PRESETS");
        const char* preset_path = preset_env ? preset_env : DEFAULT_PRESET_PATH;
        renderer.load_presets(preset_path);
    }

    Gui gui;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                running = false;
            if (event.type == SDL_KEYDOWN && !io.WantCaptureKeyboard) {
                auto key = event.key.keysym.sym;
                auto mod = event.key.keysym.mod;
                bool ctrl = mod & (KMOD_LCTRL | KMOD_LGUI | KMOD_RGUI);

                if (key == SDLK_ESCAPE)
                    running = false;
                else if (key == SDLK_q && ctrl)
                    running = false;
                else if (key == SDLK_LEFT && renderer_ok)
                    renderer.prev_preset();
                else if (key == SDLK_RIGHT && renderer_ok)
                    renderer.next_preset();
                else if (key == SDLK_r && renderer_ok)
                    renderer.random_preset();
                else if (key == SDLK_SPACE && renderer_ok)
                    renderer.toggle_lock();
                else if (key == SDLK_UP && renderer_ok)
                    renderer.adjust_beat_sensitivity(0.01f);
                else if (key == SDLK_DOWN && renderer_ok)
                    renderer.adjust_beat_sensitivity(-0.01f);
                else if (key == SDLK_a && renderer_ok)
                    renderer.toggle_aspect_correction();
                else if (key == SDLK_f)
                    SDL_SetWindowFullscreen(window,
                        (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
                            ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
        }

        if (media_ok) media.poll();
        volume.poll();

        if (renderer_ok) renderer.render();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        int win_w, win_h;
        SDL_GetWindowSize(window, &win_w, &win_h);
        gui.render(renderer, media, volume, win_w, win_h);

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        SDL_GL_SwapWindow(window);
    }

    audio.stop();
    media.shutdown();
    renderer.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    pw_deinit();

    return 0;
}
