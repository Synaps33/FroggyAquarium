// Marine Aquarium 2 - Linux handheld port (SDL2 + GLES2).
//
// Usage: aquarium [--data DIR] [--config FILE] [--screensaver] [--shot N FILE]
//   --data DIR      folder holding data1.hdr + data1.cab from the original CD
//   --config FILE   settings file (default DIR/aquarium.ini)
//   --screensaver   any button or key exits
//   --shot N FILE   render N frames at 60 Hz simulated time, save FILE (.ppm), exit
#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "cab.h"
#include "pe.h"
#include "render.h"
#include "aquarium.h"
#include "config.h"
#include "audio.h"

static const char* kDefaultData = "/usr/bin/emulationstation/resources/aquarium";

static void SavePpm(const std::string& path, int w, int h)
{
    std::vector<uint8_t> px((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--)
        for (int x = 0; x < w; x++) fwrite(&px[((size_t)y * w + x) * 4], 1, 3, f);
    fclose(f);
}

int main(int argc, char** argv)
{
    std::string dataDir = kDefaultData, cfgPath, shotPath;
    bool screensaver = false;
    int shotFrames = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dataDir = argv[++i];
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) cfgPath = argv[++i];
        else if (!strcmp(argv[i], "--screensaver")) screensaver = true;
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc) { shotFrames = atoi(argv[++i]); shotPath = argv[++i]; }
    }
    if (cfgPath.empty()) cfgPath = dataDir + "/aquarium.ini";

    Config cfg;
    if (!LoadConfig(cfgPath, cfg)) SaveConfig(cfgPath, cfg);

    std::vector<uint8_t> scr;
    std::string err;
    if (!CabExtract(dataDir + "/data1.hdr", dataDir + "/data1.cab", "Marine Aquarium 2.scr", scr, err)) {
        fprintf(stderr, "aquarium: %s\n", err.c_str());
        return 1;
    }
    std::vector<uint8_t> image = scr;
    PeResources res;
    if (!res.Load(std::move(scr), err)) {
        fprintf(stderr, "aquarium: %s\n", err.c_str());
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "aquarium: SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int w = 640, h = 480;
    Uint32 flags = SDL_WINDOW_OPENGL;
    if (!shotFrames) {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) { w = dm.w; h = dm.h; }
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    SDL_Window* win = SDL_CreateWindow("Marine Aquarium", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h, flags);
    if (!win) { fprintf(stderr, "aquarium: window: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    if (!gl) { fprintf(stderr, "aquarium: GL context: %s\n", SDL_GetError()); SDL_Quit(); return 1; }
    SDL_GL_SetSwapInterval(1);
    SDL_GL_GetDrawableSize(win, &w, &h);
    SDL_ShowCursor(SDL_DISABLE);

    Renderer r;
    if (!r.Init(w, h, &res, err)) { fprintf(stderr, "aquarium: %s\n", err.c_str()); SDL_Quit(); return 1; }
    Aquarium aq;
    if (!aq.Init(image, &res, &r, cfg, err)) { fprintf(stderr, "aquarium: %s\n", err.c_str()); SDL_Quit(); return 1; }

    if (cfg.sound && !shotFrames)
        if (const std::vector<uint8_t>* wav = res.Find("WAV", "BUBBLES6H.WAV")) AudioStart(*wav, 0.32f);

    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);
        else SDL_JoystickOpen(i);
    }

    bool running = true, lightsChanged = false;
    uint32_t simTime = 1000;
    int frame = 0;
    const float step = 0.05f;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            int action = 0;   // 1 quit, 2 fg+, 3 fg-, 4 bg+, 5 bg-, 6 bubbles
            switch (e.type) {
            case SDL_QUIT: action = 1; break;
            case SDL_KEYDOWN:
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: case SDLK_RETURN: case SDLK_BACKSPACE: action = 1; break;
                case SDLK_UP: action = 2; break;
                case SDLK_DOWN: action = 3; break;
                case SDLK_RIGHT: action = 4; break;
                case SDLK_LEFT: action = 5; break;
                case SDLK_SPACE: case SDLK_a: action = 6; break;
                default: break;
                }
                if (screensaver) action = 1;
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_START: case SDL_CONTROLLER_BUTTON_BACK: case SDL_CONTROLLER_BUTTON_GUIDE: action = 1; break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP: action = 2; break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: action = 3; break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: action = 4; break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: action = 5; break;
                case SDL_CONTROLLER_BUTTON_A: action = 6; break;
                default: break;
                }
                if (screensaver) action = 1;
                break;
            case SDL_JOYHATMOTION:
                if (SDL_IsGameController(e.jhat.which)) break;
                if (e.jhat.value & SDL_HAT_UP) action = 2;
                else if (e.jhat.value & SDL_HAT_DOWN) action = 3;
                else if (e.jhat.value & SDL_HAT_RIGHT) action = 4;
                else if (e.jhat.value & SDL_HAT_LEFT) action = 5;
                if (screensaver && e.jhat.value) action = 1;
                break;
            case SDL_JOYBUTTONDOWN:
                if (SDL_IsGameController(e.jbutton.which)) break;
                action = screensaver ? 1 : (e.jbutton.button == 0 ? 6 : 0);
                break;
            default: break;
            }
            switch (action) {
            case 1: running = false; break;
            case 2: aq.AdjustForeground(step); lightsChanged = true; break;
            case 3: aq.AdjustForeground(-step); lightsChanged = true; break;
            case 4: aq.AdjustBackground(step); lightsChanged = true; break;
            case 5: aq.AdjustBackground(-step); lightsChanged = true; break;
            case 6: aq.ToggleBubbles(); cfg.bubbles = aq.BubblesOn(); break;
            default: break;
            }
        }
        if (shotFrames) {
            simTime += 16;
            aq.Frame(simTime);
            if (++frame >= shotFrames) { SavePpm(shotPath, w, h); running = false; }
        } else {
            aq.Frame(SDL_GetTicks());
        }
        SDL_GL_SwapWindow(win);
    }

    if (!shotFrames) {
        // the original saves the starfish position on exit (0x402eb0)
        cfg.starfishPos = aq.StarfishX();
        cfg.starfishOnGlass = aq.StarfishOnGlass();
        if (lightsChanged) {
            cfg.autoLights = false;
            cfg.foregroundLights = (int)(aq.Foreground() * 100.0f + 0.5f);
            cfg.backgroundLights = (int)(aq.Background() * 100.0f + 0.5f);
        }
        SaveConfig(cfgPath, cfg);
    }
    AudioStop();
    r.Shutdown();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
