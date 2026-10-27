#include "audio.h"
#include <cstring>
#include <cstdlib>

static const int kAdapt[16] = {230, 230, 230, 230, 307, 409, 512, 614,
                               768, 614, 512, 409, 307, 230, 230, 230};
static const int kCoef1[7] = {256, 512, 0, 192, 240, 460, 392};
static const int kCoef2[7] = {0, -256, 0, 64, 0, -208, -232};

static std::vector<int16_t> s_pcm;   // interleaved stereo at 44100 Hz
static uint32_t s_pos = 0;           // 16.16 fixed source position
static int s_vol = 82;               // 0..255

static inline uint32_t R32(const uint8_t* p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t R16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

struct AdpcmState { int coef1, coef2, delta, s1, s2; };

static inline int16_t Expand(AdpcmState& st, int nib)
{
    int pred = (st.s1 * st.coef1 + st.s2 * st.coef2) / 64;
    pred += ((nib & 8) ? (nib - 16) : nib) * st.delta;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    st.s2 = st.s1;
    st.s1 = pred;
    st.delta = (kAdapt[nib] * st.delta) >> 8;
    if (st.delta < 16) st.delta = 16;
    return (int16_t)pred;
}

bool AudioLoad(const std::vector<uint8_t>& wav, int volume255)
{
    AudioStop();
    s_vol = volume255 < 0 ? 0 : (volume255 > 255 ? 255 : volume255);
    const uint8_t* d = wav.data();
    const size_t n = wav.size();
    if (n < 12 || memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return false;

    const uint8_t* fmt = nullptr;
    uint32_t fmtLen = 0;
    const uint8_t* data = nullptr;
    uint32_t dataLen = 0;
    size_t off = 12;
    while (off + 8 <= n) {
        uint32_t len = R32(d + off + 4);
        if (off + 8 + len > n) len = (uint32_t)(n - off - 8);
        if (memcmp(d + off, "fmt ", 4) == 0) { fmt = d + off + 8; fmtLen = len; }
        else if (memcmp(d + off, "data", 4) == 0) { data = d + off + 8; dataLen = len; }
        off += 8 + len + (len & 1);
    }
    if (!fmt || !data || fmtLen < 16) return false;

    int channels = R16(fmt + 2);
    uint32_t rate = R32(fmt + 4);
    int blockAlign = R16(fmt + 12);
    int bits = R16(fmt + 14);
    int format = R16(fmt + 0);
    if (format == 1 && bits == 16) {
        // Plain PCM16 stereo: use as-is (rate converted below if needed).
        if (channels != 2) return false;
        size_t frames = dataLen / 4;
        s_pcm.resize(frames * 2);
        memcpy(s_pcm.data(), data, frames * 4);
        if (rate == 44100) {
            // already at the output rate
        } else if (rate == 22050) {
            std::vector<int16_t> up(frames * 4);
            for (size_t i = 0; i < frames; i++) {
                up[i * 4 + 0] = s_pcm[i * 2 + 0];
                up[i * 4 + 1] = s_pcm[i * 2 + 1];
                up[i * 4 + 2] = s_pcm[i * 2 + 0];
                up[i * 4 + 3] = s_pcm[i * 2 + 1];
            }
            s_pcm.swap(up);
        } else {
            return false;
        }
    } else if (format == 2) {
        if (channels != 2 || fmtLen < 22) return false;
        int samplesPerBlock = R16(fmt + 18);
        int numCoef = R16(fmt + 20);
        int coef1[7], coef2[7];
        for (int i = 0; i < 7; i++) { coef1[i] = kCoef1[i]; coef2[i] = kCoef2[i]; }
        const uint8_t* ct = fmt + 22;
        if (numCoef > 7) numCoef = 7;
        for (int i = 0; i < numCoef && (uint32_t)(22 + i * 4 + 4) <= fmtLen; i++) {
            coef1[i] = (int16_t)R16(ct + i * 4);
            coef2[i] = (int16_t)R16(ct + i * 4 + 2);
        }
        if (blockAlign < 14 || samplesPerBlock < 2) return false;

        const int hdr = 7 * channels;
        const int totalBlocks = (int)(dataLen / blockAlign);
        s_pcm.reserve((size_t)totalBlocks * samplesPerBlock * 2);

        for (int b = 0; b < totalBlocks; b++) {
            const uint8_t* blk = data + (size_t)b * blockAlign;
            int predL = blk[0], predR = blk[1];
            if (predL > 6) predL = 0;
            if (predR > 6) predR = 0;
            AdpcmState L, R;
            L.coef1 = coef1[predL]; L.coef2 = coef2[predL];
            R.coef1 = coef1[predR]; R.coef2 = coef2[predR];
            L.delta = (int16_t)R16(blk + 2);
            R.delta = (int16_t)R16(blk + 4);
            L.s1 = (int16_t)R16(blk + 6);
            R.s1 = (int16_t)R16(blk + 8);
            L.s2 = (int16_t)R16(blk + 10);
            R.s2 = (int16_t)R16(blk + 12);

            s_pcm.push_back((int16_t)L.s2); s_pcm.push_back((int16_t)R.s2);
            s_pcm.push_back((int16_t)L.s1); s_pcm.push_back((int16_t)R.s1);
            for (int i = hdr; i < blockAlign; i++) {
                int v = blk[i];
                s_pcm.push_back(Expand(L, v >> 4));
                s_pcm.push_back(Expand(R, v & 0x0F));
            }
        }

        // 22050 -> 44100 (duplicate every frame)
        if (rate == 22050) {
            std::vector<int16_t> up(s_pcm.size() * 2);
            size_t frames = s_pcm.size() / 2;
            for (size_t i = 0; i < frames; i++) {
                up[i * 4 + 0] = s_pcm[i * 2 + 0];
                up[i * 4 + 1] = s_pcm[i * 2 + 1];
                up[i * 4 + 2] = s_pcm[i * 2 + 0];
                up[i * 4 + 3] = s_pcm[i * 2 + 1];
            }
            s_pcm.swap(up);
        } else if (rate != 44100) {
            return false;
        }
    } else {
        return false;
    }

    if (s_pcm.size() < 8) return false;
    // volume
    for (size_t i = 0; i < s_pcm.size(); i++) s_pcm[i] = (int16_t)((s_pcm[i] * s_vol) >> 8);
    s_pos = 0;
    return true;
}

void AudioStop()
{
    s_pcm.clear();
    s_pcm.shrink_to_fit();
    s_pos = 0;
}

void AudioRender(int16_t* out, int frames)
{
    if (s_pcm.empty()) {
        memset(out, 0, (size_t)frames * 4);
        return;
    }
    const uint32_t count = (uint32_t)(s_pcm.size() / 2);
    uint32_t pos = s_pos;
    const int16_t* pcm = s_pcm.data();
    for (int i = 0; i < frames; i++) {
        uint32_t idx = pos >> 16;
        if (idx >= count) { idx = 0; pos = 0; }
        out[i * 2 + 0] = pcm[idx * 2 + 0];
        out[i * 2 + 1] = pcm[idx * 2 + 1];
        pos += 65536;   // source is already 44.1 kHz after resampling
    }
    s_pos = pos % (count << 16);
}
