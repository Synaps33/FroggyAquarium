// Deterministic libretro test runner for the aquarium core. Drives the core
// like the console frontend would, exercises the inputs and dumps screenshots.
#include "libretro.h"
#include "audio.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

static uint16_t fb[320 * 240];
static uint16_t pad;
static int frame_no;
static char prefix[512] = "screenshots/libretro/frame";
static bool quit_seen;
static unsigned long long audio_nonzero;
static long long audio_frames;
static FILE* audio_dump;

static void video_cb(const void* data, unsigned width, unsigned height, size_t pitch)
{
    if (!data) return;
    const uint8_t* src = (const uint8_t*)data;
    for (unsigned y = 0; y < height && y < 240; y++)
        memcpy(&fb[y * 320], src + (size_t)y * pitch, (width < 320 ? width : 320) * 2);
}

static void audio_sample_cb(int16_t l, int16_t r) { (void)l; (void)r; }

static size_t audio_batch_cb(const int16_t* data, size_t frames)
{
    audio_frames += frames;
    if (data) {
        for (size_t i = 0; i < frames * 2; i++) if (data[i]) audio_nonzero++;
        if (!audio_dump && getenv("AQUARIUM_AUDIO_DUMP"))
            audio_dump = fopen(getenv("AQUARIUM_AUDIO_DUMP"), "wb");
        if (audio_dump) fwrite(data, sizeof(int16_t) * 2, frames, audio_dump);
    }
    return frames;
}

static void input_poll_cb(void) {}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)port; (void)device; (void)index;
    unsigned bit = 0;
    switch (id) {
    case RETRO_DEVICE_ID_JOYPAD_LEFT: bit = 1u << 0; break;
    case RETRO_DEVICE_ID_JOYPAD_RIGHT: bit = 1u << 1; break;
    case RETRO_DEVICE_ID_JOYPAD_UP: bit = 1u << 2; break;
    case RETRO_DEVICE_ID_JOYPAD_DOWN: bit = 1u << 3; break;
    case RETRO_DEVICE_ID_JOYPAD_A: bit = 1u << 4; break;
    case RETRO_DEVICE_ID_JOYPAD_START: bit = 1u << 5; break;
    case RETRO_DEVICE_ID_JOYPAD_SELECT: bit = 1u << 6; break;
    default: break;
    }
    return (pad & bit) ? 1 : 0;
}

static bool environ_cb(unsigned cmd, void* data)
{
    if (cmd == RETRO_ENVIRONMENT_SET_PIXEL_FORMAT) {
        enum retro_pixel_format f = *(enum retro_pixel_format*)data;
        return f == RETRO_PIXEL_FORMAT_RGB565;
    }
    if (cmd == RETRO_ENVIRONMENT_SHUTDOWN) { quit_seen = true; return true; }
    return true;
}

static void save_ppm(const char* name)
{
    char path[600];
    snprintf(path, sizeof(path), "%s_%s.ppm", prefix, name);
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n320 240\n255\n");
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t p = fb[i];
        int r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
        fputc((r5 << 3) | (r5 >> 2), f);
        fputc((g6 << 2) | (g6 >> 4), f);
        fputc((b5 << 3) | (b5 >> 2), f);
    }
    fclose(f);
    printf("[RUNNER] frame %d shot=%s\n", frame_no, path);
}

static void frames(int n)
{
    for (int i = 0; i < n; i++) { retro_run(); frame_no++; if (quit_seen) break; }
}
static void press(unsigned bits, int hold) { pad = (uint16_t)bits; frames(hold); pad = 0; frames(2); }

int main(int argc, char** argv)
{
    const char* content = argc > 1 ? argv[1] : "assets/Aquarium.gba";
    if (argc > 2) snprintf(prefix, sizeof(prefix), "%s", argv[2]);

    retro_set_environment(environ_cb);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_sample_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = content;
    if (!retro_load_game(&info)) { fprintf(stderr, "[RUNNER] load failed\n"); return 1; }

    {
        int16_t probe[256 * 2];
        AudioRender(probe, 256);
        int nz = 0;
        for (int i = 0; i < 256 * 2; i++) if (probe[i]) nz++;
        printf("[RUNNER] audio probe nonzero=%d\n", nz);
    }
    frames(3);
    save_ppm("01_start");
    frames(27);
    save_ppm("02_scene");
    press(1u << 1, 4);          // right: background brighter
    frames(6);
    save_ppm("03_bg_up");
    press(1u << 3, 4);          // down: foreground darker
    frames(6);
    save_ppm("04_fg_down");
    press(1u << 0, 4);          // left back
    press(1u << 2, 4);          // up back
    frames(10);
    save_ppm("05_restored");
    press(1u << 4, 2);          // A: bubbles off
    frames(6);
    save_ppm("06_bubbles_off");
    press(1u << 4, 2);          // A: bubbles on
    frames(6);
    save_ppm("07_bubbles_on");
    press(1u << 5, 2);          // START: shutdown request
    frames(3);

    printf("[RUNNER] frames=%d quit_seen=%d audio_frames=%lld audio_nonzero=%llu\n",
           frame_no, quit_seen ? 1 : 0, audio_frames, audio_nonzero);
    if (audio_dump) fclose(audio_dump);
    retro_unload_game();
    retro_deinit();
    return quit_seen ? 0 : 2;
}
