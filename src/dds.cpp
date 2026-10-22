// DDS (DXT1 / DXT3 / DXT5) decoder to RGBA8888, top mip level only.
#include "dds.h"
#include <cstring>

static uint32_t L32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t L16(const uint8_t* p) { return p[0] | (p[1] << 8); }

static void Rgb565(uint16_t c, uint8_t* o)
{
    o[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
    o[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
    o[2] = (uint8_t)((c & 31) * 255 / 31);
    o[3] = 255;
}

// colour block; dxt1 enables the 3-colour + transparent mode
static void ColorBlock(const uint8_t* b, uint8_t pal[4][4], bool dxt1)
{
    uint16_t c0 = L16(b), c1 = L16(b + 2);
    Rgb565(c0, pal[0]);
    Rgb565(c1, pal[1]);
    if (!dxt1 || c0 > c1) {
        for (int i = 0; i < 3; i++) {
            pal[2][i] = (uint8_t)((2 * pal[0][i] + pal[1][i]) / 3);
            pal[3][i] = (uint8_t)((pal[0][i] + 2 * pal[1][i]) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int i = 0; i < 3; i++) { pal[2][i] = (uint8_t)((pal[0][i] + pal[1][i]) / 2); pal[3][i] = 0; }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
}

bool DdsDecode(const std::vector<uint8_t>& d, int& w, int& h, std::vector<uint8_t>& rgba, std::string& err)
{
    if (d.size() < 128 || memcmp(d.data(), "DDS ", 4) != 0) { err = "not a DDS file"; return false; }
    h = (int)L32(&d[12]);
    w = (int)L32(&d[16]);
    const uint8_t* fcc = &d[84];
    int type = 0;
    if (!memcmp(fcc, "DXT1", 4)) type = 1;
    else if (!memcmp(fcc, "DXT3", 4)) type = 3;
    else if (!memcmp(fcc, "DXT5", 4)) type = 5;
    else { err = "unsupported DDS format"; return false; }
    const int bsz = (type == 1) ? 8 : 16;
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    if (d.size() < 128 + (size_t)bw * bh * bsz) { err = "DDS truncated"; return false; }

    rgba.assign((size_t)w * h * 4, 0);
    const uint8_t* p = &d[128];
    for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++, p += bsz) {
            uint8_t alpha[16];
            const uint8_t* cb = p;
            if (type == 3) {
                for (int i = 0; i < 16; i++) {
                    int nib = (p[i / 2] >> ((i & 1) * 4)) & 15;
                    alpha[i] = (uint8_t)(nib * 17);
                }
                cb = p + 8;
            } else if (type == 5) {
                uint8_t a[8];
                a[0] = p[0]; a[1] = p[1];
                if (a[0] > a[1]) for (int i = 1; i < 7; i++) a[i + 1] = (uint8_t)(((7 - i) * a[0] + i * a[1]) / 7);
                else {
                    for (int i = 1; i < 5; i++) a[i + 1] = (uint8_t)(((5 - i) * a[0] + i * a[1]) / 5);
                    a[6] = 0; a[7] = 255;
                }
                uint64_t bits = 0;
                for (int i = 0; i < 6; i++) bits |= (uint64_t)p[2 + i] << (8 * i);
                for (int i = 0; i < 16; i++) alpha[i] = a[(bits >> (3 * i)) & 7];
                cb = p + 8;
            }
            uint8_t pal[4][4];
            ColorBlock(cb, pal, type == 1);
            uint32_t idx = L32(cb + 4);
            for (int i = 0; i < 16; i++) {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x >= w || y >= h) continue;
                uint8_t* o = &rgba[((size_t)y * w + x) * 4];
                const uint8_t* c = pal[(idx >> (2 * i)) & 3];
                o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
                o[3] = (type == 1) ? c[3] : alpha[i];
            }
        }
    return true;
}
