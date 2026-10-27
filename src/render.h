// Software rasterizer for Marine Aquarium 2 on GB300 / SF2000 (320x240 RGB565)
// Replaces GLES2 with integer fixed-point software rasterization (no FPU required in raster loop).
#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include "d3dmath.h"
#include "xfile.h"

class PeResources;

struct TLVertex {           // D3DTLVERTEX, 32 bytes
    float sx, sy, sz, rhw;
    uint32_t color;         // 0xAARRGGBB or 0xAABBGGRR
    uint32_t specular;
    float tu, tv;
};
static_assert(sizeof(TLVertex) == 32, "TLVertex must be 32 bytes");

enum PrimType { PRIM_LINESTRIP = 3, PRIM_TRILIST = 4, PRIM_TRISTRIP = 5 };

struct RVert {
    int32_t x, y;      // screen 16.16
    int32_t z;         // depth 24.8 (0..65535<<8)
    int32_t u, v;      // texels 16.16
    int32_t r, g, b, a;// 0..255 in 16.16
    int32_t sr, sg, sb;// 0..255 in 16.16
};

struct ClipVert {
    float x, y, z, w;
    float u, v;
    float r, g, b, a;
    float sr, sg, sb;
};

struct SoftTexture {
    int w = 0, h = 0;
    int shift_w = 0;
    int mask_u = 0, mask_v = 0;
    bool hasAlpha = false;
    std::vector<uint32_t> pixels; // 0xAARRGGBB
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool Init(int width, int height, const PeResources* res, std::string& err);
    void Shutdown();

    void Clear(float r, float g, float b);
    void SetView(const Matrix& m) { m_view = m; }
    void SetProj(const Matrix& m) { m_proj = m; }
    void SetWorld(const Matrix& m) { m_world = m; }

    // render states (names follow D3DRENDERSTATE_*)
    void SetZEnable(bool on) { m_zEnable = on; }
    void SetZWrite(bool on) { m_zWrite = on; }
    void SetAlphaBlend(bool on) { m_alphaBlend = on; }
    void SetAlphaTest(bool on) { m_alphaTest = on; }
    void SetSpecular(bool on) { m_specularOn = on; }
    void SetClamp(bool clamp) { m_clamp = clamp; }

    // lighting
    void SetLight(const float pos[3], const float color[3]) {
        for (int i = 0; i < 3; i++) { m_lightPos[i] = pos[i]; m_lightColor[i] = color[i]; }
    }
    void SetAmbient(const float rgb[3]) {
        for (int i = 0; i < 3; i++) m_ambient[i] = rgb[i];
    }
    void SetMaterial(const XMaterial& m) { m_mat = m; }

    unsigned Texture(const std::string& name);  // 0 if missing
    void SetTexture(unsigned tex) { m_tex = tex; }

    // Lit, transformed vertices (D3DFVF_VERTEX) with optional indices
    void DrawVertices(PrimType prim, const D3DVertex* v, int nv, const uint16_t* idx, int ni);
    // Pre-transformed vertices (D3DFVF_TLVERTEX)
    void DrawTL(PrimType prim, const TLVertex* v, int nv);

    // CD3DFile::Render: textures/materials as original
    void DrawFile(XFile& f, bool noTextures, bool ownMaterials, const XMaterial& fallback, float lightLevel);

    int Width() const { return m_w; }
    int Height() const { return m_h; }

    uint16_t* Framebuffer() { return m_fb; }

    // statistics (reset by Clear)
    unsigned long long statVerts = 0, statTris = 0, statPixels = 0, statDrawn = 0, statCulled = 0, statSpans = 0;
    void SetExternalFramebuffer(uint16_t* fb) { m_extFb = fb; m_fb = fb ? fb : m_internalFb.data(); }

private:
    void DrawObject(XObject* o, const Matrix& parent, bool noTextures, bool ownMaterials,
                    const XMaterial& fallback, float lightLevel);

    void RasterizeTriangle(const RVert& v0, const RVert& v1, const RVert& v2, const SoftTexture* tex, bool tl);
    void EmitTriangle(const ClipVert& a, const ClipVert& b, const ClipVert& c, const SoftTexture* tex);
    void EmitPrepared(const RVert& a, const RVert& b, const RVert& c, const SoftTexture* tex);

    int m_w = 320, m_h = 240;
    const PeResources* m_res = nullptr;
    std::vector<uint16_t> m_internalFb;
    uint16_t* m_extFb = nullptr;
    uint16_t* m_fb = nullptr;
    std::vector<uint16_t> m_zbuf;
    struct FVert {
        int32_t cx, cy, cz, cw;                  // clip Q20.20
        int32_t u, v, r, g, b, a, sr, sg, sb;    // 16.16
    };
    std::vector<FVert> m_fv;
    std::vector<RVert> m_rproj;
    std::vector<RVert> m_rv;

    std::vector<SoftTexture> m_textures;
    std::map<std::string, unsigned> m_texCache;

    Matrix m_view, m_proj, m_world;
    bool m_zEnable = true;
    bool m_zWrite = true;
    bool m_alphaBlend = false;
    bool m_alphaTest = false;
    bool m_specularOn = false;
    bool m_clamp = false;

    float m_lightPos[3] = {0, 0, 0};
    float m_lightColor[3] = {1, 1, 1};
    float m_ambient[3] = {0, 0, 0};
    XMaterial m_mat;
    unsigned m_tex = 0;
};
