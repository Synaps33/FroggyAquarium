// Looped ambient sound (BUBBLES6H.WAV, MS-ADPCM) through SDL audio.
#include "audio.h"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>

static SDL_AudioDeviceID s_dev = 0;
static Uint8* s_buf = nullptr;
static Uint32 s_len = 0;
static Uint32 s_pos = 0;
static int s_vol = SDL_MIX_MAXVOLUME;

static void Callback(void*, Uint8* out, int len)
{
    memset(out, 0, len);
    int done = 0;
    while (done < len && s_len > 0) {
        Uint32 n = s_len - s_pos;
        if (n > (Uint32)(len - done)) n = len - done;
        SDL_MixAudioFormat(out + done, s_buf + s_pos, AUDIO_S16SYS, n, s_vol);
        done += n;
        s_pos += n;
        if (s_pos >= s_len) s_pos = 0;
    }
}

bool AudioStart(const std::vector<uint8_t>& wav, float volume)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "aquarium: audio init: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec;
    Uint8* raw = nullptr;
    Uint32 rawLen = 0;
    SDL_RWops* rw = SDL_RWFromConstMem(wav.data(), (int)wav.size());
    if (!SDL_LoadWAV_RW(rw, 1, &spec, &raw, &rawLen)) {
        fprintf(stderr, "aquarium: wav: %s\n", SDL_GetError());
        return false;
    }
    // convert to S16 at the source rate/channels
    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, spec.channels, spec.freq) < 0) {
        SDL_FreeWAV(raw);
        return false;
    }
    cvt.len = (int)rawLen;
    cvt.buf = (Uint8*)SDL_malloc((size_t)rawLen * (cvt.len_mult > 0 ? cvt.len_mult : 1));
    memcpy(cvt.buf, raw, rawLen);
    SDL_FreeWAV(raw);
    if (cvt.needed && SDL_ConvertAudio(&cvt) != 0) { SDL_free(cvt.buf); return false; }
    s_buf = cvt.buf;
    s_len = (Uint32)cvt.len_cvt;
    s_len -= s_len % (2 * spec.channels);
    s_pos = 0;
    s_vol = (int)(volume * SDL_MIX_MAXVOLUME);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = spec.freq;
    want.format = AUDIO_S16SYS;
    want.channels = spec.channels;
    want.samples = 2048;
    want.callback = Callback;
    s_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!s_dev) {
        fprintf(stderr, "aquarium: audio device: %s\n", SDL_GetError());
        SDL_free(s_buf);
        s_buf = nullptr;
        return false;
    }
    SDL_PauseAudioDevice(s_dev, 0);
    return true;
}

void AudioStop()
{
    if (s_dev) SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
    if (s_buf) SDL_free(s_buf);
    s_buf = nullptr;
    s_len = s_pos = 0;
}
