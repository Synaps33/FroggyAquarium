// libretro core for Marine Aquarium 2 (GB300 / SF2000 software-rendered port).
#include "libretro.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "cab.h"
#include "pe.h"
#include "render.h"
#include "aquarium.h"
#include "config.h"
#include "audio.h"

static retro_environment_t environ_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_sample_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static bool initialized = false;
static unsigned frame_no = 0;

static void Log(const char* fmt, ...)
{
#ifdef SF2000
    FILE* f = fopen("/mnt/sda1/aquarium.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
#else
    (void)fmt;
#endif
}

static Renderer* g_renderer = nullptr;
static Aquarium* g_aquarium = nullptr;
static PeResources* g_resources = nullptr;
static Config g_cfg;
static std::string g_cfgPath;
static bool g_lightsChanged = false;
static const float kStep = 0.05f;

#ifdef SF2000
extern "C" uint32_t os_get_tick_count(void);
#endif

void retro_set_environment(retro_environment_t cb) { environ_cb = cb; }
void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_audio_sample(retro_audio_sample_t cb) { audio_sample_cb = cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_init(void) { initialized = true; }
void retro_deinit(void)
{
    AudioStop();
    delete g_aquarium; g_aquarium = nullptr;
    delete g_renderer; g_renderer = nullptr;
    delete g_resources; g_resources = nullptr;
    initialized = false;
}

unsigned retro_api_version(void) { return RETRO_API_VERSION; }

void retro_get_system_info(struct retro_system_info *info)
{
    memset(info, 0, sizeof(*info));
    info->library_name = "Marine Aquarium 2";
    info->library_version = "0.1-sf2000";
    info->valid_extensions = "gba";
    info->need_fullpath = true;
    info->block_extract = false;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
    info->geometry.base_width = 320;
    info->geometry.base_height = 240;
    info->geometry.max_width = 320;
    info->geometry.max_height = 240;
    info->geometry.aspect_ratio = 4.0f / 3.0f;
    info->timing.fps = 60.0;
    info->timing.sample_rate = 44100.0;
}

void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
void retro_reset(void) {}
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }

static std::string DirName(const char* path)
{
    if (!path || !*path) return ".";
    std::string s(path);
    size_t p = s.find_last_of("/\\");
    if (p == std::string::npos) return ".";
    if (p == 0) return "/";
    return s.substr(0, p);
}

static bool FileExists(const std::string& p)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

static bool ReadFile(const std::string& p, std::vector<uint8_t>& out)
{
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, f) : 0;
    fclose(f);
    return got == (size_t)n;
}

static bool LoadScr(const std::string& dir, std::vector<uint8_t>& scr, std::string& err)
{
    std::string hdr = dir + "/data1.hdr";
    std::string cab = dir + "/data1.cab";
    if (FileExists(hdr) && FileExists(cab))
        return CabExtract(hdr, cab, "Marine Aquarium 2.scr", scr, err);
    if (ReadFile(dir + "/Marine Aquarium 2.scr", scr)) return true;
    if (ReadFile(dir + "/Marine_Aquarium_2.scr", scr)) return true;
    if (ReadFile(dir + "/Aquarium/Marine Aquarium 2.scr", scr)) return true;
    err = "data1.hdr/data1.cab not found in " + dir;
    return false;
}

static void LoadWav(const PeResources* res, float volume)
{
    if (!g_cfg.sound) return;
    if (const std::vector<uint8_t>* wav = res->Find("WAV", "BUBBLES6H.WAV"))
        AudioLoad(*wav, (int)(volume * 255.0f + 0.5f));
}

bool retro_load_game(const struct retro_game_info *game)
{
    const char* path = game ? game->path : nullptr;
    std::string dir = DirName(path);
    std::string err;
    Log("load_game path=%s dir=%s", path ? path : "(null)", dir.c_str());

    std::vector<uint8_t> scr;
    if (!LoadScr(dir, scr, err)) {
        // also try the parent folder (asset folder next to the ROM directory)
        std::string parent = DirName(dir.c_str());
        if (parent != dir && LoadScr(parent, scr, err)) dir = parent;
    }
    if (scr.empty()) {
        fprintf(stderr, "aquarium: %s\n", err.c_str());
        Log("load failed: %s", err.c_str());
        return false;
    }
    Log("scr extracted: %u bytes", (unsigned)scr.size());

    g_cfgPath = dir + "/aquarium.ini";
    LoadConfig(g_cfgPath, g_cfg);

    std::vector<uint8_t> image = scr;
    g_resources = new PeResources();
    if (!g_resources->Load(std::move(scr), err)) {
        fprintf(stderr, "aquarium: %s\n", err.c_str());
        Log("resource load failed: %s", err.c_str());
        return false;
    }

    g_renderer = new Renderer();
    if (!g_renderer->Init(320, 240, g_resources, err)) {
        fprintf(stderr, "aquarium: %s\n", err.c_str());
        return false;
    }
    g_aquarium = new Aquarium();
    if (!g_aquarium->Init(image, g_resources, g_renderer, g_cfg, err)) {
        fprintf(stderr, "aquarium: %s\n", err.c_str());
        return false;
    }
    image.clear();
    image.shrink_to_fit();

    LoadWav(g_resources, 0.32f);
    Log("init ok (cfg=%s sound=%d)", g_cfgPath.c_str(), (int)g_cfg.sound);
    if (environ_cb) {
        enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;
        environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt);
    }
    frame_no = 0;
    g_lightsChanged = false;
    return true;
}

bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t) { return false; }

void retro_unload_game(void)
{
    AudioStop();
    if (g_lightsChanged && g_aquarium && !g_cfgPath.empty()) {
        g_cfg.autoLights = false;
        g_cfg.foregroundLights = (int)(g_aquarium->Foreground() * 100.0f + 0.5f);
        g_cfg.backgroundLights = (int)(g_aquarium->Background() * 100.0f + 0.5f);
        SaveConfig(g_cfgPath, g_cfg);
    }
    delete g_aquarium; g_aquarium = nullptr;
    delete g_renderer; g_renderer = nullptr;
    delete g_resources; g_resources = nullptr;
}

unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
void *retro_get_memory_data(unsigned id) { (void)id; return nullptr; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }

static int16_t audio_buf[735 * 2];

void retro_run(void)
{
    if (!g_aquarium || !g_renderer) return;
    if (input_poll_cb) input_poll_cb();

    bool left = false, right = false, up = false, down = false, toggle = false, quit = false;
    if (input_state_cb) {
        auto btn = [](unsigned id) -> bool {
            return input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;
        };
        left = btn(RETRO_DEVICE_ID_JOYPAD_LEFT);
        right = btn(RETRO_DEVICE_ID_JOYPAD_RIGHT);
        up = btn(RETRO_DEVICE_ID_JOYPAD_UP);
        down = btn(RETRO_DEVICE_ID_JOYPAD_DOWN);
        toggle = btn(RETRO_DEVICE_ID_JOYPAD_A);
        quit = btn(RETRO_DEVICE_ID_JOYPAD_START) || btn(RETRO_DEVICE_ID_JOYPAD_SELECT);
    }

    static bool prevToggle = false;
    static bool prevQuit = false;
    if (toggle && !prevToggle) g_aquarium->ToggleBubbles();
    prevToggle = toggle;

    if (left) { g_aquarium->AdjustBackground(-kStep); g_lightsChanged = true; }
    if (right) { g_aquarium->AdjustBackground(kStep); g_lightsChanged = true; }
    if (up) { g_aquarium->AdjustForeground(kStep); g_lightsChanged = true; }
    if (down) { g_aquarium->AdjustForeground(-kStep); g_lightsChanged = true; }

    frame_no++;
#ifdef SF2000
    uint32_t now = (uint32_t)os_get_tick_count();
#else
    uint32_t now = (uint32_t)((uint64_t)frame_no * 1000u / 60u);
#endif
    g_aquarium->Frame(now);

    if (video_cb)
        video_cb(g_renderer->Framebuffer(), 320, 240, 320 * sizeof(uint16_t));

    AudioRender(audio_buf, 735);
    if (audio_batch_cb) audio_batch_cb(audio_buf, 735);

    if (quit && !prevQuit && environ_cb) {
        environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, nullptr);
    }
    prevQuit = quit;
}
