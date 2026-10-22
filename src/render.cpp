// GLES2 stand-in for the Direct3D 6 fixed-function pipeline.
#include "render.h"
#include "pe.h"
#include "dds.h"
#include <GLES2/gl2.h>
#include <cstdio>
#include <cmath>
#include <cctype>
#include <cstring>

static const char* kLitVS =
    "attribute vec3 aPos;\n"
    "attribute vec3 aNrm;\n"
    "attribute vec2 aUV;\n"
    "uniform mat4 uWorld;\n"
    "uniform mat4 uViewProj;\n"
    "uniform vec3 uEye;\n"
    "uniform vec3 uLightDir;\n"      // world space, towards the light
    "uniform vec3 uLightColor;\n"
    "uniform vec3 uAmbient;\n"
    "uniform vec4 uDiffuse;\n"
    "uniform vec3 uMatAmbient;\n"
    "uniform vec3 uSpecular;\n"
    "uniform vec3 uEmissive;\n"
    "uniform float uPower;\n"
    "varying vec4 vColor;\n"
    "varying vec3 vSpec;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "  vec4 wp = uWorld * vec4(aPos, 1.0);\n"
    "  vec3 n = normalize((uWorld * vec4(aNrm, 0.0)).xyz);\n"
    "  float ndl = max(dot(n, uLightDir), 0.0);\n"
    "  vec3 c = uEmissive + uAmbient * uMatAmbient + uDiffuse.rgb * uLightColor * ndl;\n"
    "  vColor = vec4(clamp(c, 0.0, 1.0), uDiffuse.a);\n"
    "  vec3 s = vec3(0.0);\n"
    "  if (ndl > 0.0) {\n"
    "    vec3 h = normalize(uLightDir + normalize(uEye - wp.xyz));\n"
    "    float ndh = max(dot(n, h), 0.0);\n"
    "    s = uSpecular * uLightColor * (uPower > 0.0 ? pow(ndh, uPower) : 1.0);\n"
    "  }\n"
    "  vSpec = clamp(s, 0.0, 1.0);\n"
    "  vUV = aUV;\n"
    "  vec4 p = uViewProj * wp;\n"
    "  gl_Position = vec4(p.x, p.y, 2.0 * p.z - p.w, p.w);\n"
    "}\n";

static const char* kLitFS =
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uHasTex;\n"
    "uniform float uAlphaRef;\n"
    "uniform float uSpecOn;\n"
    "varying vec4 vColor;\n"
    "varying vec3 vSpec;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "  vec4 t = texture2D(uTex, vUV);\n"
    "  vec4 c = vec4(vColor.rgb * t.rgb, uHasTex > 0.5 ? t.a : vColor.a);\n"
    "  if (c.a < uAlphaRef) discard;\n"
    "  c.rgb = min(c.rgb + vSpec * uSpecOn, 1.0);\n"
    "  gl_FragColor = c;\n"
    "}\n";

static const char* kTLVS =
    "attribute vec4 aPos;\n"
    "attribute vec4 aCol;\n"
    "attribute vec2 aUV;\n"
    "uniform vec2 uScreen;\n"
    "varying vec4 vColor;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "  vColor = aCol.bgra;\n"           // D3DCOLOR is BGRA in memory
    "  vUV = aUV;\n"
    "  gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, aPos.z * 2.0 - 1.0, 1.0);\n"
    "}\n";

static const char* kTLFS =
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "uniform float uAlphaRef;\n"
    "varying vec4 vColor;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "  vec4 c = vColor * texture2D(uTex, vUV);\n"
    "  if (c.a < uAlphaRef) discard;\n"
    "  gl_FragColor = c;\n"
    "}\n";

static GLuint Compile(GLenum type, const char* src, std::string& err)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        err = log;
        return 0;
    }
    return s;
}

static GLuint Link(const char* vs, const char* fs, const char* const* attrs, int na, std::string& err)
{
    GLuint v = Compile(GL_VERTEX_SHADER, vs, err);
    if (!v) return 0;
    GLuint f = Compile(GL_FRAGMENT_SHADER, fs, err);
    if (!f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    for (int i = 0; i < na; i++) glBindAttribLocation(p, i, attrs[i]);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        err = log;
        return 0;
    }
    return p;
}

bool Renderer::Init(int width, int height, const PeResources* res, std::string& err)
{
    m_w = width;
    m_h = height;
    m_res = res;
    static const char* litAttrs[] = {"aPos", "aNrm", "aUV"};
    static const char* tlAttrs[] = {"aPos", "aCol", "aUV"};
    m_progLit = Link(kLitVS, kLitFS, litAttrs, 3, err);
    if (!m_progLit) { err = "lit shader: " + err; return false; }
    m_progTL = Link(kTLVS, kTLFS, tlAttrs, 3, err);
    if (!m_progTL) { err = "tl shader: " + err; return false; }
    auto L = [&](const char* n) { return glGetUniformLocation(m_progLit, n); };
    m_lit = {L("uWorld"), L("uViewProj"), L("uEye"), L("uLightDir"), L("uLightColor"), L("uAmbient"),
             L("uDiffuse"), L("uMatAmbient"), L("uSpecular"), L("uEmissive"), L("uPower"), L("uHasTex"),
             L("uAlphaRef"), L("uSpecOn"), L("uTex")};
    m_tl = {glGetUniformLocation(m_progTL, "uScreen"), glGetUniformLocation(m_progTL, "uAlphaRef"),
            glGetUniformLocation(m_progTL, "uTex")};

    uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &m_white);
    glBindTexture(GL_TEXTURE_2D, m_white);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glViewport(0, 0, width, height);
    glDepthFunc(GL_LEQUAL);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glFrontFace(GL_CW);   // D3D default D3DCULL_CCW: clockwise faces are front
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    return true;
}

void Renderer::Shutdown()
{
    for (auto& t : m_texCache) if (t.second) glDeleteTextures(1, &t.second);
    m_texCache.clear();
    if (m_white) glDeleteTextures(1, &m_white);
    if (m_progLit) glDeleteProgram(m_progLit);
    if (m_progTL) glDeleteProgram(m_progTL);
    m_white = m_progLit = m_progTL = 0;
}

void Renderer::Clear(float r, float g, float b)
{
    glDepthMask(GL_TRUE);
    glClearColor(r, g, b, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::SetZEnable(bool on) { if (on) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST); }
void Renderer::SetZWrite(bool on) { glDepthMask(on ? GL_TRUE : GL_FALSE); }
void Renderer::SetAlphaBlend(bool on) { if (on) glEnable(GL_BLEND); else glDisable(GL_BLEND); }

unsigned Renderer::Texture(const std::string& name)
{
    std::string key;
    for (char c : name) key += (char)tolower((unsigned char)c);
    auto it = m_texCache.find(key);
    if (it != m_texCache.end()) return it->second;

    unsigned tex = 0;
    std::string up;
    for (char c : key) up += (char)toupper((unsigned char)c);
    const std::vector<uint8_t>* data = m_res ? m_res->Find("10", up) : nullptr;
    if (data) {
        int w, h;
        std::vector<uint8_t> px;
        std::string err;
        if (DdsDecode(*data, w, h, px, err)) {
            // The art is authored with v=0 at the bottom row (all meshes agree),
            // so store rows bottom-up.
            std::vector<uint8_t> row((size_t)w * 4);
            for (int y = 0; y < h / 2; y++) {
                uint8_t* a = &px[(size_t)y * w * 4];
                uint8_t* b = &px[(size_t)(h - 1 - y) * w * 4];
                memcpy(row.data(), a, row.size());
                memcpy(a, b, row.size());
                memcpy(b, row.data(), row.size());
            }
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            fprintf(stderr, "aquarium: texture %s: %s\n", key.c_str(), err.c_str());
        }
    }
    m_texCache[key] = tex;
    return tex;
}

void Renderer::BindTexture()
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_tex ? m_tex : m_white);
    GLint wrap = m_clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

static GLenum GlPrim(PrimType p)
{
    switch (p) {
    case PRIM_TRISTRIP: return GL_TRIANGLE_STRIP;
    case PRIM_LINESTRIP: return GL_LINE_STRIP;
    default: return GL_TRIANGLES;
    }
}

void Renderer::DrawVertices(PrimType prim, const D3DVertex* v, int nv, const uint16_t* idx, int ni)
{
    if (nv <= 0) return;
    glUseProgram(m_progLit);
    Matrix vp;
    MatMul(vp, m_view, m_proj);

    // parallel point light: direction from the object origin to the light
    float ox = m_world.m[3][0], oy = m_world.m[3][1], oz = m_world.m[3][2];
    float lx = m_lightPos[0] - ox, ly = m_lightPos[1] - oy, lz = m_lightPos[2] - oz;
    float l = sqrtf(lx * lx + ly * ly + lz * lz);
    if (l > 1e-6f) { lx /= l; ly /= l; lz /= l; } else { lx = 0; ly = 1; lz = 0; }

    // eye position from inverse view (view is orthonormal: eye = -T * R^T)
    float ex = -(m_view.m[3][0] * m_view.m[0][0] + m_view.m[3][1] * m_view.m[0][1] + m_view.m[3][2] * m_view.m[0][2]);
    float ey = -(m_view.m[3][0] * m_view.m[1][0] + m_view.m[3][1] * m_view.m[1][1] + m_view.m[3][2] * m_view.m[1][2]);
    float ez = -(m_view.m[3][0] * m_view.m[2][0] + m_view.m[3][1] * m_view.m[2][1] + m_view.m[3][2] * m_view.m[2][2]);

    glUniformMatrix4fv(m_lit.world, 1, GL_FALSE, m_world.f());
    glUniformMatrix4fv(m_lit.viewProj, 1, GL_FALSE, vp.f());
    glUniform3f(m_lit.eye, ex, ey, ez);
    glUniform3f(m_lit.lightDir, lx, ly, lz);
    glUniform3fv(m_lit.lightColor, 1, m_lightColor);
    glUniform3fv(m_lit.ambient, 1, m_ambient);
    glUniform4fv(m_lit.diffuse, 1, m_mat.diffuse);
    glUniform3fv(m_lit.matAmbient, 1, m_mat.ambient);
    glUniform3fv(m_lit.specular, 1, m_mat.specular);
    glUniform3fv(m_lit.emissive, 1, m_mat.emissive);
    glUniform1f(m_lit.power, m_mat.power);
    glUniform1f(m_lit.hasTex, m_tex ? 1.0f : 0.0f);
    glUniform1f(m_lit.alphaRef, m_alphaTest ? 32.0f / 255.0f : -1.0f);
    glUniform1f(m_lit.specOn, m_specularOn ? 1.0f : 0.0f);
    glUniform1i(m_lit.tex, 0);
    BindTexture();

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(D3DVertex), &v->x);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(D3DVertex), &v->nx);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(D3DVertex), &v->tu);
    if (idx) glDrawElements(GlPrim(prim), ni, GL_UNSIGNED_SHORT, idx);
    else glDrawArrays(GlPrim(prim), 0, nv);
}

void Renderer::DrawTL(PrimType prim, const TLVertex* v, int nv)
{
    glUseProgram(m_progTL);
    glUniform2f(m_tl.screen, (float)m_w, (float)m_h);
    glUniform1f(m_tl.alphaRef, m_alphaTest ? 32.0f / 255.0f : -1.0f);
    glUniform1i(m_tl.tex, 0);
    BindTexture();
    glDisable(GL_CULL_FACE);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(TLVertex), &v->sx);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(TLVertex), &v->color);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(TLVertex), &v->tu);
    glDrawArrays(GlPrim(prim), 0, nv);
    glEnable(GL_CULL_FACE);
}

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
            // 0x40cc80 only draws groups that name a texture which loaded
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
    Matrix ident;
    DrawObject(f.Root(), ident, noTextures, ownMaterials, fallback, lightLevel);
}
