// GLES2 stand-in for the Direct3D 6 fixed-function pipeline used by the
// aquarium: per-vertex D3D lighting (one parallel-point light + ambient +
// material emissive), texture modulate + specular add, alpha test/blend,
// screen-space (pre-transformed) vertices, D3D row-vector matrices.
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
    uint32_t color;         // 0xAARRGGBB
    uint32_t specular;
    float tu, tv;
};
static_assert(sizeof(TLVertex) == 32, "TLVertex must be 32 bytes");

enum PrimType { PRIM_TRILIST = 4, PRIM_TRISTRIP = 5, PRIM_LINESTRIP = 3 };

class Renderer {
public:
    bool Init(int width, int height, const PeResources* res, std::string& err);
    void Shutdown();

    void Clear(float r, float g, float b);
    void SetView(const Matrix& m) { m_view = m; }
    void SetProj(const Matrix& m) { m_proj = m; }
    void SetWorld(const Matrix& m) { m_world = m; }

    // render states (names follow D3DRENDERSTATE_*)
    void SetZEnable(bool on);
    void SetZWrite(bool on);
    void SetAlphaBlend(bool on);
    void SetAlphaTest(bool on) { m_alphaTest = on; }
    void SetSpecular(bool on) { m_specularOn = on; }
    void SetClamp(bool clamp) { m_clamp = clamp; }

    // lighting
    void SetLight(const float pos[3], const float color[3]) { for (int i = 0; i < 3; i++) { m_lightPos[i] = pos[i]; m_lightColor[i] = color[i]; } }
    void SetAmbient(const float rgb[3]) { for (int i = 0; i < 3; i++) m_ambient[i] = rgb[i]; }
    void SetMaterial(const XMaterial& m) { m_mat = m; }

    unsigned Texture(const std::string& name);  // 0 if missing
    void SetTexture(unsigned tex) { m_tex = tex; }

    // Lit, transformed vertices (D3DFVF_VERTEX) with optional indices
    void DrawVertices(PrimType prim, const D3DVertex* v, int nv, const uint16_t* idx, int ni);
    // Pre-transformed vertices (D3DFVF_TLVERTEX)
    void DrawTL(PrimType prim, const TLVertex* v, int nv);

    // CD3DFile::Render: textures/materials as the original 0x40cc80
    void DrawFile(XFile& f, bool noTextures, bool ownMaterials, const XMaterial& fallback, float lightLevel);

    int Width() const { return m_w; }
    int Height() const { return m_h; }

private:
    void DrawObject(XObject* o, const Matrix& parent, bool noTextures, bool ownMaterials,
                    const XMaterial& fallback, float lightLevel);
    void BindTexture();
    unsigned m_progLit = 0, m_progTL = 0;
    struct LitLoc {
        int world, viewProj, eye, lightDir, lightColor, ambient, diffuse, matAmbient,
            specular, emissive, power, hasTex, alphaRef, specOn, tex;
    } m_lit = {};
    struct TLLoc { int screen, alphaRef, tex; } m_tl = {};
    int m_w = 0, m_h = 0;
    const PeResources* m_res = nullptr;
    std::map<std::string, unsigned> m_texCache;
    Matrix m_view, m_proj, m_world;
    bool m_alphaTest = false, m_specularOn = false, m_clamp = false;
    float m_lightPos[3] = {0, 0, 0}, m_lightColor[3] = {1, 1, 1}, m_ambient[3] = {0, 0, 0};
    XMaterial m_mat;
    unsigned m_tex = 0;
    unsigned m_white = 0;
};
