// Software rasterizer for the Marine Aquarium 2 port (GB300 / SF2000).
//
// Replaces the GLES2 stand-in with a scanline rasterizer that writes RGB565
// into a 320x240 framebuffer with a 16-bit z-buffer. The pixel inner loops use
// only integer arithmetic (the target MIPS CPU has no FPU); floating point is
// confined to per-vertex and per-triangle setup, where the volume is small.
//
// The lit path reproduces the original Direct3D 6 fixed-function behaviour as
// the GLES port did: one parallel-point light evaluated per vertex (ambient +
// diffuse + optional specular), texture modulate, alpha test at 32/255 and
// straight alpha blending.
#include "render.h"
#include "pe.h"
#include "dds.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdlib>

// Profiling hooks (environment driven, no effect in normal runs).
static bool s_noDraw = false;
static bool s_noRaster = false;
static int s_cullSign = 0;
static bool s_noSpan = false;
static bool s_noVertex = false;   // 0 = no culling, +1/-1 pick the front-face winding

// ---------------------------------------------------------------- colours
static inline uint16_t Pack565(int r, int g, int b)
{
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// 4x4 ordered dither (Bayer) to hide RGB565 banding on gradients.
static const uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static inline uint16_t Pack565d(int r, int g, int b, int d)
{
    r += ((d * 8) >> 4) - 4;
    g += ((d * 4) >> 4) - 2;
    b += ((d * 8) >> 4) - 4;
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline void Unpack565(uint16_t c, int& r, int& g, int& b)
{
    int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
    r = (r5 << 3) | (r5 >> 2);
    g = (g6 << 2) | (g6 >> 4);
    b = (b5 << 3) | (b5 >> 2);
}

static inline uint16_t Color755To565(float r, float g, float b)
{
    return Pack565((int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(b * 255.0f + 0.5f));
}

// ---------------------------------------------------------------- textures
static bool DecodeTexture(const std::vector<uint8_t>& dds, SoftTexture& out, std::string& err)
{
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
    if (!DdsDecode(dds, w, h, rgba, err)) return false;
    if (w <= 0 || h <= 0 || (w & (w - 1)) || (h & (h - 1))) { err = "texture not power-of-two"; return false; }

    out.w = w;
    out.h = h;
    out.shift_w = 0;
    while ((1 << out.shift_w) < w) out.shift_w++;
    out.mask_u = w - 1;
    out.mask_v = h - 1;
    out.pixels.resize((size_t)w * h);
    out.hasAlpha = false;

    // The art is authored with v=0 at the bottom row (the GLES port flipped
    // the rows before upload for the same reason), so store rows bottom-up.
    for (int y = 0; y < h; y++) {
        const uint8_t* src = &rgba[(size_t)(h - 1 - y) * w * 4];
        uint32_t* dst = &out.pixels[(size_t)y * w];
        for (int x = 0; x < w; x++) {
            uint32_t r = src[x * 4 + 0], g = src[x * 4 + 1], b = src[x * 4 + 2], a = src[x * 4 + 3];
            if (a != 255) out.hasAlpha = true;
            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return true;
}

// ---------------------------------------------------------------- span state
struct TriSetup {
    int yStart, yEnd;
    int32_t xLongFx, xShortFx;
    int32_t longStep, shortStep;
    int32_t phaseY;       // v1.y
    int32_t phaseX;       // v1.x
    int32_t phaseStep;    // slope of the v1->v2 edge
    bool longLeft;
    bool shortPhase2;
    int32_t gx[10], gy[10], attr[10];
};

// The whole scanline walk lives in this template: one dispatch per triangle
// instead of one per span removes most of the fixed cost of the many tiny
// spans (fish fins, caustics) that dominate this scene.
template <bool HASTEX, bool BLEND, bool ATEST, bool SPEC, bool TLALPHA>
static void ScanTriangle(const TriSetup& t, const SoftTexture* tx,
                         uint16_t* fbBase, uint16_t* zbBase, int w, int h,
                         bool zwrite, bool clamp)
{
    const int shift = HASTEX ? tx->shift_w : 0;
    const int mu = HASTEX ? tx->mask_u : 0;
    const int mv = HASTEX ? tx->mask_v : 0;
    const uint32_t* pix = HASTEX ? tx->pixels.data() : nullptr;
    const int tw = HASTEX ? tx->w : 0;
    const int th = HASTEX ? tx->h : 0;

    int32_t xLongFx = t.xLongFx, xShortFx = t.xShortFx;
    const int32_t longStep = t.longStep;
    int32_t shortStep = t.shortStep;
    bool shortPhase2 = t.shortPhase2;
    int32_t attr[10];
    for (int i = 0; i < 10; i++) attr[i] = t.attr[i];

    int prevLeft = 0x7FFFFFFF;

    for (int y = t.yStart; y < t.yEnd; y++) {
        const int32_t yc = ((int32_t)y << 16) + 0x8000;
        if (!shortPhase2 && yc >= t.phaseY) {
            xShortFx = t.phaseX + (int32_t)(((int64_t)(yc - t.phaseY) * t.phaseStep) >> 16);
            shortStep = t.phaseStep;
            shortPhase2 = true;
        }
        const int32_t xLeftFx = t.longLeft ? xLongFx : xShortFx;
        const int32_t xRightFx = t.longLeft ? xShortFx : xLongFx;

        int x0 = (xLeftFx + 0x8000) >> 16;
        int x1 = (xRightFx + 0x8000) >> 16;
        if (x0 < 0) x0 = 0;
        if (x1 > w) x1 = w;

        if (x0 < x1) {
            const int32_t anchor = (prevLeft == 0x7FFFFFFF) ? 0 : prevLeft;
            if (x0 != anchor) {
                const int64_t dx = (int64_t)x0 - anchor;
                for (int i = 0; i < 10; i++) {
                    int64_t vv = (int64_t)attr[i] + (int64_t)t.gx[i] * dx;
                    if (vv > 0x3FFFFFFF) vv = 0x3FFFFFFF;
                    if (vv < -0x3FFFFFFF) vv = -0x3FFFFFFF;
                    attr[i] = (int32_t)vv;
                }
            }
            prevLeft = x0;

            // ---- pixel loop ----
            uint16_t* fb = fbBase + (size_t)y * w + x0;
            uint16_t* zb = zbBase + (size_t)y * w + x0;
            const int n = x1 - x0;
            int32_t z = attr[0], u = attr[1], v = attr[2];
            int32_t r = attr[3], g = attr[4], b = attr[5], a = attr[6];
            int32_t sr = attr[7], sg = attr[8], sb = attr[9];
            const int32_t dz = t.gx[0], du = t.gx[1], dv = t.gx[2];
            const int32_t dr = t.gx[3], dg = t.gx[4], db = t.gx[5], da = t.gx[6];
            const int32_t dsr = t.gx[7], dsg = t.gx[8], dsb = t.gx[9];
            int ci = x0 & 3;
            const int rowPhase = (y & 3) << 2;

            for (int i = 0; i < n; i++) {
                uint16_t zi = (uint16_t)(z >> 8);
                bool vis = !zwrite || zi < zb[i];
                if (vis) {
                    int vr = r >> 16, vg = g >> 16, vb = b >> 16, va = a >> 16;
                    if (vr < 0) vr = 0; else if (vr > 255) vr = 255;
                    if (vg < 0) vg = 0; else if (vg > 255) vg = 255;
                    if (vb < 0) vb = 0; else if (vb > 255) vb = 255;
                    int tr, tg, tb, ta;
                    if (HASTEX) {
                        int uu = u >> 16, vv = v >> 16;
                        if (clamp) {
                            if (uu < 0) uu = 0; else if (uu >= tw) uu = tw - 1;
                            if (vv < 0) vv = 0; else if (vv >= th) vv = th - 1;
                        } else {
                            uu &= mu;
                            vv &= mv;
                        }
                        uint32_t texel = pix[(vv << shift) + uu];
                        tr = (texel >> 16) & 255;
                        tg = (texel >> 8) & 255;
                        tb = texel & 255;
                        ta = texel >> 24;
                        if (TLALPHA) {
                            if (va < 0) va = 0; else if (va > 255) va = 255;
                            ta = (ta * va) >> 8;
                        }
                        tr = (tr * vr) >> 8;
                        tg = (tg * vg) >> 8;
                        tb = (tb * vb) >> 8;
                    } else {
                        tr = vr; tg = vg; tb = vb; ta = va;
                    }

                    bool keep = !ATEST || ta >= 32;
                    if (keep) {
                        if (SPEC) {
                            tr += sr >> 16;
                            tg += sg >> 16;
                            tb += sb >> 16;
                            if (tr > 255) tr = 255;
                            if (tg > 255) tg = 255;
                            if (tb > 255) tb = 255;
                        }
                        if (BLEND) {
                            uint16_t d = fb[i];
                            int dr2 = (d >> 11) & 31; dr2 = (dr2 << 3) | (dr2 >> 2);
                            int dg2 = (d >> 5) & 63;  dg2 = (dg2 << 2) | (dg2 >> 4);
                            int db2 = d & 31;         db2 = (db2 << 3) | (db2 >> 2);
                            tr = (tr * ta + dr2 * (255 - ta)) >> 8;
                            tg = (tg * ta + dg2 * (255 - ta)) >> 8;
                            tb = (tb * ta + db2 * (255 - ta)) >> 8;
                        }
                        fb[i] = Pack565d(tr, tg, tb, kBayer[rowPhase | ci]);
                        if (zwrite) zb[i] = zi;
                    }
                }
                z += dz;
                if (HASTEX) { u += du; v += dv; }
                r += dr; g += dg; b += db;
                if (TLALPHA) a += da;
                if (SPEC) { sr += dsr; sg += dsg; sb += dsb; }
                ci = (ci + 1) & 3;
            }
        }

        xLongFx += longStep;
        xShortFx += shortStep;
        for (int i = 0; i < 10; i++) attr[i] += t.gy[i];
    }
}

static inline void PackAndDispatch(const TriSetup& t, const SoftTexture* tex,
                                   uint16_t* fb, uint16_t* zb, int w, int h,
                                   bool zwrite, bool clamp, bool tl,
                                   bool blend, bool atest, bool spec)
{
    if (tex) {
        if (tl) {
            if (blend) { if (atest) ScanTriangle<true, true, true, false, true>(t, tex, fb, zb, w, h, zwrite, clamp); else ScanTriangle<true, true, false, false, true>(t, tex, fb, zb, w, h, zwrite, clamp); }
            else { if (atest) ScanTriangle<true, false, true, false, true>(t, tex, fb, zb, w, h, zwrite, clamp); else ScanTriangle<true, false, false, false, true>(t, tex, fb, zb, w, h, zwrite, clamp); }
        } else if (spec) {
            if (blend) { if (atest) ScanTriangle<true, true, true, true, false>(t, tex, fb, zb, w, h, zwrite, clamp); else ScanTriangle<true, true, false, true, false>(t, tex, fb, zb, w, h, zwrite, clamp); }
            else { if (atest) ScanTriangle<true, false, true, true, false>(t, tex, fb, zb, w, h, zwrite, clamp); else ScanTriangle<true, false, false, true, false>(t, tex, fb, zb, w, h, zwrite, clamp); }
        } else {
            if (blend) { if (atest) ScanTriangle<true, true, true, false, false>(t, tex, fb, zb, w, h, zwrite, clamp); else ScanTriangle<true, true, false, false, false>(t, tex, fb, zb, w, h, zwrite, clamp); }
            else { if (atest) ScanTriangle<true, false, true, false, false>(t, tex, fb, zb, w, h, zwrite, clamp); else ScanTriangle<true, false, false, false, false>(t, tex, fb, zb, w, h, zwrite, clamp); }
        }
    } else {
        if (blend) { if (atest) ScanTriangle<false, true, true, false, false>(t, tex, fb, zb, w, h, zwrite, clamp); else ScanTriangle<false, true, false, false, false>(t, tex, fb, zb, w, h, zwrite, clamp); }
        else { if (atest) ScanTriangle<false, false, true, false, false>(t, tex, fb, zb, w, h, zwrite, clamp); else ScanTriangle<false, false, false, false, false>(t, tex, fb, zb, w, h, zwrite, clamp); }
    }
}

// ---------------------------------------------------------------- renderer
Renderer::Renderer() {}

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(int width, int height, const PeResources* res, std::string& err)
{
    m_w = width;
    m_h = height;
    m_res = res;
    m_internalFb.assign((size_t)m_w * m_h, 0);
    m_zbuf.assign((size_t)m_w * m_h, 0xFFFF);
    if (!m_extFb) m_fb = m_internalFb.data();
    m_textures.clear();
    m_texCache.clear();
    s_noDraw = getenv("AQUARIUM_NODRAW") != nullptr;
    s_noRaster = getenv("AQUARIUM_NORASTER") != nullptr;
    s_noSpan = getenv("AQUARIUM_NOSPAN") != nullptr;
    s_noVertex = getenv("AQUARIUM_NOVERTEX") != nullptr;
    {
        const char* cs = getenv("AQUARIUM_CULL");
        s_cullSign = cs ? atoi(cs) : 0;
    }
    err.clear();
    return true;
}

void Renderer::Shutdown()
{
    m_textures.clear();
    m_texCache.clear();
    m_internalFb.clear();
    m_zbuf.clear();
    m_fb = nullptr;
}

void Renderer::Clear(float r, float g, float b)
{
    if (s_noDraw) return;
    statVerts = statTris = statPixels = statDrawn = statCulled = statSpans = 0;
    uint16_t c = Color755To565(r, g, b);
    size_t n = (size_t)m_w * m_h;
    uint32_t pat = ((uint32_t)c << 16) | c;
    uint32_t* fb32 = reinterpret_cast<uint32_t*>(m_fb);
    size_t n32 = n / 2;
    for (size_t i = 0; i < n32; i++) fb32[i] = pat;
    if (n & 1) m_fb[n - 1] = c;
    memset(m_zbuf.data(), 0xFF, n * sizeof(uint16_t));
}

unsigned Renderer::Texture(const std::string& name)
{
    std::string key;
    for (char c : name) key += (char)tolower((unsigned char)c);
    auto it = m_texCache.find(key);
    if (it != m_texCache.end()) return it->second;

    unsigned id = 0;
    std::string up;
    for (char c : key) up += (char)toupper((unsigned char)c);
    const std::vector<uint8_t>* data = m_res ? m_res->Find("10", up) : nullptr;
    if (data) {
        SoftTexture tex;
        std::string err;
        if (DecodeTexture(*data, tex, err)) {
            m_textures.push_back(std::move(tex));
            id = (unsigned)m_textures.size();
        } else {
            fprintf(stderr, "aquarium: texture %s: %s\n", key.c_str(), err.c_str());
        }
    }
    m_texCache[key] = id;
    return id;
}

// ---------------------------------------------------------------- triangle
// All triangle setup is integer: positions and attributes arrive as fixed
// point, so the only divisions are the edge slopes and the inverse area.
static inline int32_t SlopeFx(int32_t dx, int32_t dy)
{
    return (int32_t)(((int64_t)dx << 16) / dy);
}

void Renderer::RasterizeTriangle(const RVert& a, const RVert& b, const RVert& c, const SoftTexture* tex, bool tl)
{
    RVert v0 = a, v1 = b, v2 = c;
    if (v0.y > v1.y) { RVert t = v0; v0 = v1; v1 = t; }
    if (v0.y > v2.y) { RVert t = v0; v0 = v2; v2 = t; }
    if (v1.y > v2.y) { RVert t = v1; v1 = v2; v2 = t; }

    const int32_t height = v2.y - v0.y;
    if (height < 64) return;                       // < 0.001 px
    if (v2.y <= 0 || v0.y >= (m_h << 16)) return;

    // Signed area in 2^32 px^2.
    const int64_t area64 = ((int64_t)v1.x - v0.x) * ((int64_t)v2.y - v0.y)
                         - ((int64_t)v2.x - v0.x) * ((int64_t)v1.y - v0.y);
    if (area64 == 0) return;
    if (!tl && s_cullSign > 0 && area64 > 0) { statCulled++; return; }
    if (!tl && s_cullSign < 0 && area64 < 0) { statCulled++; return; }
    // Slivers below 0.5 px^2 cover at most a pixel or two; skip them.
    if (area64 > -(1LL << 31) && area64 < (1LL << 31)) return;

    // K32 = 2^32 / area(px^2), computed from the 2^32-scaled area. Keeping the
    // reciprocal in 64 bits is what makes the gradients accurate on large
    // triangles (a 16.16 reciprocal would truncate to 1 for a full-screen quad).
    const int64_t areaAbs = area64 < 0 ? -area64 : area64;
    int64_t k32 = ((int64_t)1 << 62) / (areaAbs >> 2);   // 2^62 / (area_px << 30)
    if (area64 < 0) k32 = -k32;                          // preserve the winding sign

    int yStart = (int)((v0.y + 0x7FFF) >> 16);
    int yEnd = (int)((v2.y + 0x7FFF) >> 16);       // exclusive
    if (yStart < 0) yStart = 0;
    if (yEnd > m_h) yEnd = m_h;
    if (yStart >= yEnd) return;

    // Planar gradients in fixed point.
    int32_t gx[10], gy[10], cc[10];
    const int32_t A0[10] = {v0.z, v0.u, v0.v, v0.r, v0.g, v0.b, v0.a, v0.sr, v0.sg, v0.sb};
    const int32_t A1[10] = {v1.z, v1.u, v1.v, v1.r, v1.g, v1.b, v1.a, v1.sr, v1.sg, v1.sb};
    const int32_t A2[10] = {v2.z, v2.u, v2.v, v2.r, v2.g, v2.b, v2.a, v2.sr, v2.sg, v2.sb};
    const int32_t dy0 = v2.y - v0.y, dy1 = v1.y - v0.y;
    const int32_t dx0 = v1.x - v0.x, dx2 = v2.x - v0.x;
    const bool needSpec = m_specularOn;
    for (int i = 0; i < 10; i++) {
        if (i >= 7 && !needSpec) { gx[i] = gy[i] = cc[i] = 0; continue; }
        int64_t nx = (int64_t)(A1[i] - A0[i]) * dy0 - (int64_t)(A2[i] - A0[i]) * dy1;
        int64_t ny = (int64_t)dx0 * (A2[i] - A0[i]) - (int64_t)dx2 * (A1[i] - A0[i]);
        int64_t gxi = ((nx >> 32) * k32) >> 16;
        int64_t gyi = ((ny >> 32) * k32) >> 16;
        // z is scaled 24.8 while the other attributes are 16.16.
        if (i == 0) { gxi >>= 8; gyi >>= 8; }
        if (gxi > 0x3FFFFFFF) gxi = 0x3FFFFFFF; else if (gxi < -0x3FFFFFFF) gxi = -0x3FFFFFFF;
        if (gyi > 0x3FFFFFFF) gyi = 0x3FFFFFFF; else if (gyi < -0x3FFFFFFF) gyi = -0x3FFFFFFF;
        gx[i] = (int32_t)gxi;
        gy[i] = (int32_t)gyi;
        int64_t c64 = (int64_t)A0[i] - (((int64_t)gxi * v0.x) >> 16) - (((int64_t)gyi * v0.y) >> 16);
        if (c64 > 0x3FFFFFFF) c64 = 0x3FFFFFFF; else if (c64 < -0x3FFFFFFF) c64 = -0x3FFFFFFF;
        cc[i] = (int32_t)c64;
    }

    // Edge slopes (screen x per scanline) and the first scanline values.
    const int32_t longStep = SlopeFx(v2.x - v0.x, height);
    const bool longLeft = area64 > 0;
    int32_t shortStep;
    bool shortPhase2;
    if (v1.y <= v0.y) {
        shortStep = SlopeFx(v2.x - v1.x, v2.y - v1.y);
        shortPhase2 = true;
    } else {
        shortStep = SlopeFx(v1.x - v0.x, v1.y - v0.y);
        shortPhase2 = false;
    }

    const int32_t yFirst = ((int32_t)yStart << 16) + 0x8000;
    int32_t xLongFx = v0.x + (int32_t)(((int64_t)(yFirst - v0.y) * longStep) >> 16);
    int32_t xShortFx;
    if (shortPhase2) xShortFx = v1.x + (int32_t)(((int64_t)(yFirst - v1.y) * shortStep) >> 16);
    else xShortFx = v0.x + (int32_t)(((int64_t)(yFirst - v0.y) * shortStep) >> 16);

    // Seed row attributes at x = 0.
    int32_t attr[10];
    {
        const int64_t yc = yFirst;
        for (int i = 0; i < 10; i++) {
            int64_t vv = (int64_t)cc[i] + (((int64_t)gy[i] * yc) >> 16);
            if (vv > 0x3FFFFFFF) vv = 0x3FFFFFFF;
            if (vv < -0x3FFFFFFF) vv = -0x3FFFFFFF;
            attr[i] = (int32_t)vv;
        }
    }

    const bool blend = m_alphaBlend;
    const bool atest = m_alphaTest;
    const bool spec = m_specularOn;
    const bool zwrite = m_zEnable && m_zWrite;
    const bool clamp = m_clamp;

    TriSetup ts;
    ts.yStart = yStart;
    ts.yEnd = yEnd;
    ts.xLongFx = xLongFx;
    ts.xShortFx = xShortFx;
    ts.longStep = longStep;
    ts.shortStep = shortStep;
    ts.phaseY = v1.y;
    ts.phaseX = v1.x;
    ts.phaseStep = (v2.y > v1.y) ? SlopeFx(v2.x - v1.x, v2.y - v1.y) : 0;
    ts.longLeft = longLeft;
    ts.shortPhase2 = shortPhase2;
    for (int i = 0; i < 10; i++) { ts.gx[i] = gx[i]; ts.gy[i] = gy[i]; ts.attr[i] = attr[i]; }

    // Fully opaque textures do not need the (expensive) blend path even when
    // the draw state has blending enabled.
    const bool effBlend = blend && (!tex || tex->hasAlpha);
    if (!s_noSpan && !s_noRaster)
        PackAndDispatch(ts, tex, m_fb, m_zbuf.data(), m_w, m_h, zwrite, clamp, tl, effBlend, atest, spec);
}

// ---------------------------------------------------------------- clipping
static void InterpClip(ClipVert& o, const ClipVert& a, const ClipVert& b, float t)
{
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.z = a.z + (b.z - a.z) * t;
    o.w = a.w + (b.w - a.w) * t;
    o.u = a.u + (b.u - a.u) * t;
    o.v = a.v + (b.v - a.v) * t;
    o.r = a.r + (b.r - a.r) * t;
    o.g = a.g + (b.g - a.g) * t;
    o.b = a.b + (b.b - a.b) * t;
    o.a = a.a + (b.a - a.a) * t;
    o.sr = a.sr + (b.sr - a.sr) * t;
    o.sg = a.sg + (b.sg - a.sg) * t;
    o.sb = a.sb + (b.sb - a.sb) * t;
}

// Project a clipped vertex into screen space.
static inline int32_t Q16(float v) { return (int32_t)lrintf(v * 65536.0f); }

static void Project(const ClipVert& c, RVert& o, int w, int h)
{
    float iw = 1.0f / c.w;
    float sx = (c.x * iw * 0.5f + 0.5f) * (float)w;
    float sy = (0.5f - c.y * iw * 0.5f) * (float)h;
    // Clamp so the 16.16 conversion cannot overflow on vertices very close to
    // the near plane; such vertices only feed off-screen triangles anyway.
    if (sx > 8192.0f) sx = 8192.0f; else if (sx < -8192.0f) sx = -8192.0f;
    if (sy > 8192.0f) sy = 8192.0f; else if (sy < -8192.0f) sy = -8192.0f;
    float z = c.z * iw;
    if (z < 0.0f) z = 0.0f;
    if (z > 1.0f) z = 1.0f;
    o.x = Q16(sx);
    o.y = Q16(sy);
    o.z = (int32_t)lrintf(z * 16776960.0f);
    o.u = Q16(c.u);
    o.v = Q16(c.v);
    o.r = Q16(c.r); o.g = Q16(c.g); o.b = Q16(c.b); o.a = Q16(c.a);
    o.sr = Q16(c.sr); o.sg = Q16(c.sg); o.sb = Q16(c.sb);
}


static void ProjectInt(int32_t cx, int32_t cy, int32_t cz, int32_t cw,
                       int32_t u, int32_t v, int32_t r, int32_t g, int32_t b, int32_t a,
                       int32_t sr, int32_t sg, int32_t sb, RVert& o, int w, int h)
{
    // cw = w * 2^20; only vertices in front of the near plane reach here, so
    // w >= 1 and the Q16.16 reciprocal fits in 32 bits.
    const int32_t invw = (int32_t)(((int64_t)1 << 36) / cw);
    const int64_t xn = ((int64_t)cx * invw) >> 20;          // NDC x, Q16.16
    const int64_t yn = ((int64_t)cy * invw) >> 20;          // NDC y, Q16.16
    int64_t z24 = (((int64_t)cz * invw) >> 12);             // 24.8 depth
    int64_t sx = ((xn * w) >> 1) + ((int64_t)w << 15);
    int64_t sy = ((int64_t)h << 15) - ((yn * h) >> 1);
    if (sx > (8192LL << 16)) sx = 8192LL << 16; else if (sx < -(8192LL << 16)) sx = -(8192LL << 16);
    if (sy > (8192LL << 16)) sy = 8192LL << 16; else if (sy < -(8192LL << 16)) sy = -(8192LL << 16);
    if (z24 < 0) z24 = 0; else if (z24 > 16776960) z24 = 16776960;
    o.x = (int32_t)sx;
    o.y = (int32_t)sy;
    o.z = (int32_t)z24;
    o.u = u; o.v = v;
    o.r = r; o.g = g; o.b = b; o.a = a;
    o.sr = sr; o.sg = sg; o.sb = sb;
}

void Renderer::EmitPrepared(const RVert& a, const RVert& b, const RVert& c, const SoftTexture* tex)
{
    if (s_noDraw || s_noRaster) return;
    statTris++;
    RasterizeTriangle(a, b, c, tex, false);
}

void Renderer::EmitTriangle(const ClipVert& a, const ClipVert& b, const ClipVert& c, const SoftTexture* tex)
{
    if (s_noRaster) return;
    ClipVert poly[4];
    ClipVert in[3] = {a, b, c};
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const ClipVert& p = in[i];
        const ClipVert& q = in[(i + 1) % 3];
        bool pin = p.z >= 0.0f;
        bool qin = q.z >= 0.0f;
        if (pin) poly[n++] = p;
        if (pin != qin) {
            float t = p.z / (p.z - q.z);
            InterpClip(poly[n], p, q, t);
            n++;
        }
    }
    if (n < 3) return;
    RVert rv[4];
    for (int i = 0; i < n; i++) Project(poly[i], rv[i], m_w, m_h);
    for (int i = 1; i + 1 < n; i++) {
        statTris++;
        RasterizeTriangle(rv[0], rv[i], rv[i + 1], tex, false);
    }
}

// ---------------------------------------------------------------- lit draws
// pow(ndh, power) lookup: powers in the art are 0/1/10/16/32/50/64.
struct SpecLut { float power; float v[512]; int32_t q[512]; };
static SpecLut s_specLuts[8];
static int s_specLutCount = 0;

static const int32_t* SpecularTableQ(float power)
{
    for (int i = 0; i < s_specLutCount; i++)
        if (s_specLuts[i].power == power) return s_specLuts[i].q;
    if (s_specLutCount >= 8) return nullptr;
    SpecLut& l = s_specLuts[s_specLutCount++];
    l.power = power;
    for (int i = 0; i < 512; i++) {
        float x = (i + 0.5f) / 512.0f;
        float p = power > 0.0f ? powf(x, power) : 1.0f;
        l.v[i] = p;
        l.q[i] = (int32_t)lrintf(p * 65536.0f);
    }
    return l.q;
}

// Integer 1/sqrt helpers for the specular half vector.
static uint32_t s_invLenLut[8193];
static bool s_invLenReady = false;

static void BuildInvLenLut()
{
    s_invLenLut[0] = 1u << 24;
    for (uint32_t i = 1; i <= 8192; i++) s_invLenLut[i] = (1u << 24) / i;
    s_invLenReady = true;
}

static inline uint32_t ISqrt32(uint32_t n)
{
    uint32_t res = 0;
    uint32_t bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= res + bit) { n -= res + bit; res = (res >> 1) + bit; }
        else res >>= 1;
        bit >>= 2;
    }
    return res;
}

void Renderer::DrawVertices(PrimType prim, const D3DVertex* v, int nv, const uint16_t* idx, int ni)
{
    if (s_noDraw || s_noVertex || nv <= 0) return;
    const SoftTexture* tex = (m_tex > 0 && m_tex <= m_textures.size()) ? &m_textures[m_tex - 1] : nullptr;
    const float texW = tex ? (float)tex->w : 1.0f;
    const float texH = tex ? (float)tex->h : 1.0f;
    if (!s_invLenReady) BuildInvLenLut();

    Matrix wvp;
    MatMul(wvp, m_world, m_view);
    Matrix tmp = wvp;
    MatMul(wvp, tmp, m_proj);

    // parallel point light: direction from the object origin to the light
    const float ox = m_world.m[3][0], oy = m_world.m[3][1], oz = m_world.m[3][2];
    float lxf = m_lightPos[0] - ox, lyf = m_lightPos[1] - oy, lzf = m_lightPos[2] - oz;
    float ll = sqrtf(lxf * lxf + lyf * lyf + lzf * lzf);
    if (ll > 1e-6f) { lxf /= ll; lyf /= ll; lzf /= ll; } else { lxf = 0; lyf = 1; lzf = 0; }

    // eye position from inverse view (view is orthonormal)
    const float exf = -(m_view.m[3][0] * m_view.m[0][0] + m_view.m[3][1] * m_view.m[0][1] + m_view.m[3][2] * m_view.m[0][2]);
    const float eyf = -(m_view.m[3][0] * m_view.m[1][0] + m_view.m[3][1] * m_view.m[1][1] + m_view.m[3][2] * m_view.m[1][2]);
    const float ezf = -(m_view.m[3][0] * m_view.m[2][0] + m_view.m[3][1] * m_view.m[2][1] + m_view.m[3][2] * m_view.m[2][2]);

    const XMaterial& mat = m_mat;
    const bool specOn = m_specularOn;
    const int32_t* specQ = specOn ? SpecularTableQ(mat.power) : nullptr;

    // fixed-point matrices and material constants
    int32_t mw[16];
    const float* wf = wvp.f();
    for (int k = 0; k < 16; k++) mw[k] = (int32_t)lrintf(wf[k] * 4096.0f);
    int32_t wm[9], wt[3];
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) wm[row * 3 + col] = (int32_t)lrintf(m_world.m[row][col] * 4096.0f);
        wt[row] = (int32_t)lrintf(m_world.m[3][row] * 256.0f);
    }
    int32_t lq[3] = { (int32_t)lrintf(lxf * 16384.0f), (int32_t)lrintf(lyf * 16384.0f), (int32_t)lrintf(lzf * 16384.0f) };
    int32_t eye8[3] = { (int32_t)lrintf(exf * 256.0f), (int32_t)lrintf(eyf * 256.0f), (int32_t)lrintf(ezf * 256.0f) };
    int32_t amb[3], dif[3], emi[3], spc[3];
    for (int c = 0; c < 3; c++) {
        amb[c] = (int32_t)lrintf(m_ambient[c] * mat.ambient[c] * 4096.0f);
        dif[c] = (int32_t)lrintf(mat.diffuse[c] * m_lightColor[c] * 4096.0f);
        emi[c] = (int32_t)lrintf(mat.emissive[c] * 4096.0f);
        spc[c] = (int32_t)lrintf(mat.specular[c] * m_lightColor[c] * 4096.0f);
    }
    const int32_t alpha16 = (int32_t)lrintf(mat.diffuse[3] * 65536.0f);
    const float tuScale = texW * 65536.0f;
    const float tvScale = texH * 65536.0f;

    m_fv.resize(nv);
    m_rproj.resize(nv);
    std::vector<FVert>& fv = m_fv;
    std::vector<RVert>& proj = m_rproj;
    statVerts += (unsigned)nv;

    for (int i = 0; i < nv; i++) {
        const D3DVertex& s = v[i];
        const int32_t px = (int32_t)(s.x * 256.0f);
        const int32_t py = (int32_t)(s.y * 256.0f);
        const int32_t pz = (int32_t)(s.z * 256.0f);
        const int32_t nx14 = (int32_t)(s.nx * 16384.0f);
        const int32_t ny14 = (int32_t)(s.ny * 16384.0f);
        const int32_t nz14 = (int32_t)(s.nz * 16384.0f);

        // world position (Q8.8) and world normal (Q1.14)
        const int32_t wpx = ((px * wm[0] + py * wm[3] + pz * wm[6]) >> 12) + wt[0];
        const int32_t wpy = ((px * wm[1] + py * wm[4] + pz * wm[7]) >> 12) + wt[1];
        const int32_t wpz = ((px * wm[2] + py * wm[5] + pz * wm[8]) >> 12) + wt[2];
        const int32_t nwx = (nx14 * wm[0] + ny14 * wm[3] + nz14 * wm[6]) >> 12;
        const int32_t nwy = (nx14 * wm[1] + ny14 * wm[4] + nz14 * wm[7]) >> 12;
        const int32_t nwz = (nx14 * wm[2] + ny14 * wm[5] + nz14 * wm[8]) >> 12;

        int32_t ndl = (nwx * lq[0] + nwy * lq[1] + nwz * lq[2]) >> 14;
        if (ndl < 0) ndl = 0; else if (ndl > 16384) ndl = 16384;

        int32_t cr = emi[0] + amb[0] + ((dif[0] * ndl) >> 14);
        int32_t cg = emi[1] + amb[1] + ((dif[1] * ndl) >> 14);
        int32_t cb = emi[2] + amb[2] + ((dif[2] * ndl) >> 14);
        if (cr > 4096) cr = 4096;
        if (cg > 4096) cg = 4096;
        if (cb > 4096) cb = 4096;

        int32_t sr16 = 0, sg16 = 0, sb16 = 0;
        if (specOn && ndl > 0 && specQ) {
            const int32_t vx = eye8[0] - wpx, vy = eye8[1] - wpy, vz = eye8[2] - wpz;
            const uint32_t v2 = (uint32_t)(vx * vx + vy * vy + vz * vz);
            if (v2 > 0) {
                uint32_t vl = ISqrt32(v2);
                if (vl > 8192) vl = 8192;
                const int32_t inv = (int32_t)s_invLenLut[vl];
                const int32_t ux = (vx * inv) >> 16;
                const int32_t uy = (vy * inv) >> 16;
                const int32_t uz = (vz * inv) >> 16;
                const int32_t hx = (lq[0] >> 6) + ux;
                const int32_t hy = (lq[1] >> 6) + uy;
                const int32_t hz = (lq[2] >> 6) + uz;
                const uint32_t h2 = (uint32_t)(hx * hx + hy * hy + hz * hz);
                if (h2 > 0) {
                    uint32_t hl = ISqrt32(h2);
                    if (hl > 8192) hl = 8192;
                    const int32_t invh = (int32_t)s_invLenLut[hl];
                    int64_t ndh = (int64_t)(nwx >> 6) * hx + (int64_t)(nwy >> 6) * hy + (int64_t)(nwz >> 6) * hz;
                    ndh = (ndh * invh) >> 16;
                    if (ndh < 0) ndh = 0; else if (ndh > 65536) ndh = 65536;
                    int idx = (int)(ndh >> 7);
                    if (idx > 511) idx = 511;
                    const int32_t p = specQ[idx];
                    sr16 = ((spc[0] * p) >> 16) * 4080;
                    sg16 = ((spc[1] * p) >> 16) * 4080;
                    sb16 = ((spc[2] * p) >> 16) * 4080;
                }
            }
        }

        FVert& o = fv[i];
        o.cx = px * mw[0] + py * mw[4] + pz * mw[8] + 256 * mw[12];
        o.cy = px * mw[1] + py * mw[5] + pz * mw[9] + 256 * mw[13];
        o.cz = px * mw[2] + py * mw[6] + pz * mw[10] + 256 * mw[14];
        o.cw = px * mw[3] + py * mw[7] + pz * mw[11] + 256 * mw[15];
        o.u = (int32_t)(s.tu * tuScale);
        o.v = (int32_t)(s.tv * tvScale);
        o.r = cr * 4080;
        o.g = cg * 4080;
        o.b = cb * 4080;
        o.a = alpha16;
        o.sr = sr16; o.sg = sg16; o.sb = sb16;
    }

    // Project only vertices in front of the near plane; clipped triangles are
    // rare and go through the float path below.
    for (int i = 0; i < nv; i++) {
        const FVert& f = fv[i];
        if (f.cz >= 0) {
            ProjectInt(f.cx, f.cy, f.cz, f.cw, f.u, f.v, f.r, f.g, f.b, f.a,
                       f.sr, f.sg, f.sb, proj[i], m_w, m_h);
        }
    }

    auto clipVert = [&](const FVert& f) {
        ClipVert c;
        const float is = 1.0f / 1048576.0f;
        c.x = (float)f.cx * is; c.y = (float)f.cy * is;
        c.z = (float)f.cz * is; c.w = (float)f.cw * is;
        const float ia = 1.0f / 65536.0f;
        c.u = (float)f.u * ia; c.v = (float)f.v * ia;
        c.r = (float)f.r * ia; c.g = (float)f.g * ia; c.b = (float)f.b * ia; c.a = (float)f.a * ia;
        c.sr = (float)f.sr * ia; c.sg = (float)f.sg * ia; c.sb = (float)f.sb * ia;
        return c;
    };

    auto emit = [&](int i0, int i1, int i2) {
        const FVert& a = fv[i0];
        const FVert& b = fv[i1];
        const FVert& c = fv[i2];
        if (a.cz >= 0 && b.cz >= 0 && c.cz >= 0)
            EmitPrepared(proj[i0], proj[i1], proj[i2], tex);
        else
            EmitTriangle(clipVert(a), clipVert(b), clipVert(c), tex);
    };
    if (idx) {
        for (int i = 0; i + 2 < ni; i += 3)
            emit(idx[i], idx[i + 1], idx[i + 2]);
    } else if (prim == PRIM_TRILIST) {
        for (int i = 0; i + 2 < nv; i += 3)
            emit(i, i + 1, i + 2);
    } else if (prim == PRIM_TRISTRIP) {
        for (int i = 0; i + 2 < nv; i++) {
            if (i & 1) emit(i + 1, i, i + 2);
            else emit(i, i + 1, i + 2);
        }
    }
}

void Renderer::DrawTL(PrimType prim, const TLVertex* v, int nv)
{
    if (s_noDraw || nv <= 0) return;
    const SoftTexture* tex = (m_tex > 0 && m_tex <= m_textures.size()) ? &m_textures[m_tex - 1] : nullptr;
    const float texW = tex ? (float)tex->w : 1.0f;
    const float texH = tex ? (float)tex->h : 1.0f;

    m_rv.resize(nv);
    std::vector<RVert>& rv = m_rv;
    for (int i = 0; i < nv; i++) {
        const TLVertex& s = v[i];
        RVert& o = rv[i];
        o.x = Q16(s.sx);
        o.y = Q16(s.sy);
        o.z = (int32_t)lrintf(s.sz * 16776960.0f);
        o.u = Q16(s.tu * texW);
        o.v = Q16(s.tv * texH);
        o.r = (int32_t)((s.color >> 16) & 255) << 16;
        o.g = (int32_t)((s.color >> 8) & 255) << 16;
        o.b = (int32_t)(s.color & 255) << 16;
        o.a = (int32_t)(s.color >> 24) << 16;
        o.sr = o.sg = o.sb = 0;
    }
    if (prim == PRIM_TRILIST) {
        for (int i = 0; i + 2 < nv; i += 3) RasterizeTriangle(rv[i], rv[i + 1], rv[i + 2], tex, true);
    } else if (prim == PRIM_TRISTRIP) {
        for (int i = 0; i + 2 < nv; i++) {
            if (i & 1) RasterizeTriangle(rv[i + 1], rv[i], rv[i + 2], tex, true);
            else RasterizeTriangle(rv[i], rv[i + 1], rv[i + 2], tex, true);
        }
    }
}

// ---------------------------------------------------------------- file draw
void Renderer::DrawObject(XObject* o, const Matrix& parent, bool noTextures, bool ownMaterials,
                          const XMaterial& fallback, float lightLevel)
{
    Matrix world = parent;
    if (o->isFrame) MatMul(world, o->mat, parent);
    if (!o->vertices.empty()) {
        SetWorld(world);
        for (auto& g : o->groups) {
            if (g.indices.empty()) continue;
            unsigned tex = 0;
            if (!g.material.texture.empty() && !noTextures) tex = Texture(g.material.texture);
            if (!noTextures && (g.material.texture.empty() || !tex)) continue;
            SetTexture(tex);
            SetMaterial((ownMaterials && lightLevel >= 0.2f) ? g.material : fallback);
            DrawVertices(PRIM_TRILIST, o->vertices.data(), (int)o->vertices.size(), g.indices.data(), (int)g.indices.size());
        }
        SetTexture(0);
    }
    if (!o->order.empty()) {
        for (XObject* c : o->order) DrawObject(c, world, noTextures, ownMaterials, fallback, lightLevel);
    } else {
        for (auto& c : o->children) DrawObject(c.get(), world, noTextures, ownMaterials, fallback, lightLevel);
    }
}

void Renderer::DrawFile(XFile& f, bool noTextures, bool ownMaterials, const XMaterial& fallback, float lightLevel)
{
    if (s_noDraw) return;
    Matrix ident;
    DrawObject(f.Root(), ident, noTextures, ownMaterials, fallback, lightLevel);
}
