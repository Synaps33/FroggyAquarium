// Marine Aquarium 2 scene logic, ported from the decompiled Windows .scr.
// Function names carry the original addresses; field offsets are kept as in
// the original object / data segment so the port can be checked line by line
// against the decompile.
#include "aquarium.h"
#include "pe.h"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <strings.h>
#include <ctime>

// ---------------------------------------------------------------- memory access
#define GADDR(a)  (&g_mem[(size_t)((a) - 0x424000u)])
#define G(T, a)   (*reinterpret_cast<T*>(GADDR(a)))
#define GF(a)     G(float, a)
#define GI(a)     G(int32_t, a)
#define GU(a)     G(uint32_t, a)
#define GS(a)     G(int16_t, a)
#define GB(a)     G(uint8_t, a)
#define GM(a)     G(Matrix, a)
#define GD(a)     G(double, a)
#define O(T, o)   (*reinterpret_cast<T*>(&m_obj[(size_t)(o)]))
#define OF(o)     O(float, o)
#define OI(o)     O(int32_t, o)
#define OU(o)     O(uint32_t, o)
#define OB(o)     O(uint8_t, o)
#define OS(o)     O(int16_t, o)
#define OM(o)     O(Matrix, o)
#define VF(p, o)  (*reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(p) + (o)))
#define VP(p, o)  (reinterpret_cast<uint8_t*>(p) + (o))

// per-fish shorthands (global arrays)
#define PX(i)     GF(0x42d4b8 + (i) * 12)
#define PY(i)     GF(0x42d4bc + (i) * 12)
#define PZ(i)     GF(0x42d4c0 + (i) * 12)
#define RX(i)     GF(0x42eb18 + (i) * 12)
#define RY(i)     GF(0x42eb1c + (i) * 12)
#define RZ(i)     GF(0x42eb20 + (i) * 12)
#define FTYPE(i)  GI(0x42ee60 + (i) * 4)
#define PHASE(i)  GF(0x42ee84 + (i) * 4)
#define FISHON(i) GS(0x427e2c + (i) * 2)
#define DT        ((float)GU(0x42eefc))

static const uint32_t kMemBase = 0x424000, kMemEnd = 0x433000;
static const size_t kObjSize = 0x7a00;

// MSVC rand()/srand()
static uint32_t s_seed = 1;
static void msrand(uint32_t s) { s_seed = s; }
static int msrand_() { s_seed = s_seed * 214013u + 2531011u; return (int)((s_seed >> 16) & 0x7fff); }
#define rand_ msrand_

// Fish table (0x430110): index 0 off, 1 random, 2.. names
static const char* const kFishNames[] = {
    "< OFF >", "< RANDOM >",
    "Yellow_Tang", "Golden_Butterflyfish", "Powder_Blue_Tang", "Flame_Angel", "Queen_Angel",
    "Royal_Gramma", "Copperband_Butterflyfish", "Addis_Butterflyfish", "French_Angel", "Wimplefish",
    "Forceps_Fish", "Regal_Tang", "Clown_Triggerfish", "Percula_Clownfish", "Purple_Tang",
    "Picasso_Triggerfish", "Regal_Angel", "Blue_Damselfish", "Koran_Angel", "Three_Stripe_Damselfish",
    "LionFish", "EmperorAngel", "BlueRingAngel", "RedfinBfly", "RockBeauty", "Squarespot",
};
static const int kNumFishNames = sizeof(kFishNames) / sizeof(kFishNames[0]);

static std::string ResourceForFish(const std::string& name)
{
    std::string s = name + ".X";
    if (strcasecmp(name.c_str(), "Three_Stripe_Damselfish") == 0) s = "THREE-STRIPE_DAMSELFISH.X";
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}

static void SetTranslation(Matrix* m, float x, float y, float z)
{
    m->Identity();
    m->m[3][0] = x; m->m[3][1] = y; m->m[3][2] = z;
}

Aquarium::Aquarium() : g_mem(kMemEnd - kMemBase, 0), m_obj(kObjSize, 0) {}

// ============================================================================
bool Aquarium::Init(const std::vector<uint8_t>& img, const PeResources* res, Renderer* r,
                    const Config& cfg, std::string& err)
{
    m_res = res;
    m_r = r;
    m_cfg = cfg;

    // map the image's sections into g_mem (rdata/data; bss stays zero)
    auto r32 = [&](size_t o) -> uint32_t {
        return o + 4 <= img.size() ? (img[o] | (img[o + 1] << 8) | (img[o + 2] << 16) | ((uint32_t)img[o + 3] << 24)) : 0;
    };
    uint32_t pe = r32(0x3c);
    uint16_t nsect = (uint16_t)(r32(pe + 6) & 0xffff);
    uint16_t optsz = (uint16_t)(r32(pe + 20) & 0xffff);
    uint32_t imageBase = r32(pe + 24 + 28);
    size_t sect = pe + 24 + optsz;
    for (int s = 0; s < nsect; s++, sect += 40) {
        uint32_t va = imageBase + r32(sect + 12), raw = r32(sect + 20), rsz = r32(sect + 16);
        for (uint32_t k = 0; k < rsz; k++) {
            uint32_t a = va + k;
            if (a >= kMemBase && a < kMemEnd && raw + k < img.size()) g_mem[a - kMemBase] = img[raw + k];
        }
    }

    // static initializer 0x402340: starfish start position
    GF(0x42d268) = -3.5f;
    GF(0x42d26c) = -3.7f;
    GF(0x42d270) = -0.6f;

    // settings -> original globals (0x403590)
    GI(0x427ddc) = m_cfg.fishCount < 1 ? 1 : (m_cfg.fishCount > 8 ? 8 : m_cfg.fishCount);
    GI(0x427e48) = m_cfg.sound;
    GI(0x427e44) = m_cfg.bubbles;
    GI(0x427e28) = m_cfg.autoLights;
    GB(0x427e4d) = m_cfg.starfishAllowedGlass;
    GB(0x430661) = m_cfg.noDuplicateRandom;
    GB(0x430658) = m_cfg.calcNormals;
    GB(0x430660) = m_cfg.limit60;
    GF(0x42d268) = m_cfg.starfishPos;
    GI(0x4306a8) = m_cfg.starfishOnGlass ? 1 : 0;
    GF(0x427e18) = m_cfg.foregroundLights * 0.01f;
    GF(0x427e14) = m_cfg.backgroundLights * 0.01f;
    if (GF(0x427e14) < 0.2f && GF(0x427e18) < 0.2f) {
        if (GF(0x427e14) <= GF(0x427e18)) GF(0x427e18) = 0.2f;
        else GF(0x427e14) = 0.2f;
    }
    if (GI(0x427e28) != 0) { GF(0x427e18) = 1.0f; GF(0x427e14) = 1.0f; }

    m_slots.clear();
    for (int k = 0; k < GI(0x427ddc); k++) {
        std::string want = k < (int)m_cfg.fish.size() ? m_cfg.fish[k] : "random";
        std::string name = "< RANDOM >";
        if (strcasecmp(want.c_str(), "off") == 0) name = "< OFF >";
        else
            for (int n = 2; n < kNumFishNames; n++)
                if (strcasecmp(want.c_str(), kFishNames[n]) == 0) name = kFishNames[n];
        m_slots.push_back(name);
    }
    for (int k = 0; k < 12; k++) GS(0x427e2c + k * 2) = 1;   // all fish enabled

    Construct();
    if (!OneTimeSceneInit(err)) return false;
    InitDeviceObjects();
    return true;
}

// 0x4040c0 (scene-related part of the constructor)
void Aquarium::Construct()
{
    GI(0x42cd68) = GI(0x427ddc);
    GI(0x42ce38) = 0;
    Matrix& m = OM(0x7208);
    m.Identity();
    m.m[3][0] = -1.0f;

    memset(GADDR(0x430340), 0, 0x50);
    memset(GADDR(0x42ec60), 0, 0x50);
    // default material 0x430340: diffuse a=1, ambient a=1, emissive = 1
    GF(0x430350) = 1.0f; GF(0x430360) = 1.0f;
    GF(0x430374) = 1.0f; GF(0x430378) = 1.0f; GF(0x43037c) = 1.0f; GF(0x430380) = 1.0f;
    // bubble material 0x42ec60
    GF(0x42ec70) = 1.0f; GF(0x42ec80) = 1.0f;
    GF(0x42ec94) = 1.0f; GF(0x42ec98) = 1.0f; GF(0x42ec9c) = 1.0f; GF(0x42eca0) = 1.0f;
}

// D3DMATERIAL at a global address -> XMaterial
static XMaterial MaterialAt(const uint8_t* p)
{
    XMaterial m;
    const float* f = reinterpret_cast<const float*>(p + 4);
    for (int k = 0; k < 4; k++) {
        m.diffuse[k] = f[k];
        m.ambient[k] = f[4 + k];
        m.specular[k] = f[8 + k];
        m.emissive[k] = f[12 + k];
    }
    m.power = f[16];
    return m;
}

// ============================================================================
// 0x4042e0  fish selection and loading
bool Aquarium::LoadFish(std::string& err)
{
    GI(0x42ce38) = 0;
    m_chosen.assign(GI(0x427ddc), std::string());
    for (int k = 0; k < GI(0x427ddc); k++) m_chosen[k] = m_slots[k];

    msrand((uint32_t)time(nullptr));   // original: srand(timeGetTime())
    int count = 0;
    for (int k = 0; k < GI(0x427ddc); k++) {
        if (count >= GI(0x42cd68)) break;
        std::string name = m_chosen[k];
        if (name == "< RANDOM >") {
            name = kFishNames[2 + (rand_() * 0x1a) / 0x7fff];
            if (GB(0x430661) != 0) {
                bool dup;
                do {
                    dup = false;
                    for (int q = 0; q < GI(0x427ddc); q++)
                        if (name == m_chosen[q]) { dup = true; break; }
                    if (dup) name = kFishNames[2 + (rand_() * 0x1a) / 0x7fff];
                } while (dup);
            }
            m_chosen[k] = name;
        }
        if (name == "< OFF >") continue;

        int type = 0;
        if (name == "Clown_Triggerfish" || name == "Picasso_Triggerfish") type = 1;
        if (name == "LionFish") type = 2;
        if (name == "EmperorAngel") type = 3;
        if (name == "RedfinBfly") type = 4;

        const std::vector<uint8_t>* data = m_res->Find("10", ResourceForFish(name));
        auto f = std::make_unique<XFile>();
        std::string e;
        if (!data || !f->Load(*data, e)) {
            fprintf(stderr, "aquarium: cannot load %s: %s\n", name.c_str(), data ? e.c_str() : "missing");
            continue;
        }
        FTYPE(count) = type;
        m_fishFile[count] = std::move(f);
        count++;
    }
    GI(0x42ce38) = count;
    if (count == 0) { err = "no fish could be loaded"; return false; }

    for (int i = 0; i < count; i++) {
        XFile& f = *m_fishFile[i];
        uint32_t nBody = 0, nBend = 0, nEyes = 0, nR = 0, nL = 0, nLL = 0, nRL = 0;
        m_v958[i] = f.GetMeshVertices("fishbody", &nBody);
        m_v9b8[i] = f.GetMeshVertices("fishbend", &nBend);
        m_v9e8[i] = f.GetMeshVertices("eyes", &nEyes);
        m_va48[i] = f.GetMeshVertices("rightfin", &nR);
        m_va18[i] = f.GetMeshVertices("leftfin", &nL);
        OI(0x7248 + i * 4) = (int32_t)nBody;
        if (m_v958[i] && m_v9b8[i] && m_v9e8[i] && m_va48[i] && m_va18[i]) {
            memcpy(&m_obj[0x3958 + i * 0x220], m_va48[i], std::min<size_t>(nR * 32, 0x220));
            memcpy(&m_obj[0x1fd8 + i * 0x220], m_va18[i], std::min<size_t>(nL * 32, 0x220));
        }
        m_va78[i] = f.GetMeshVertices("leftlowfin", &nLL);
        m_vaa8[i] = f.GetMeshVertices("rightlowfin", &nRL);
        if (m_va78[i] && m_vaa8[i] && m_v958[i] && m_v9b8[i] && m_v9e8[i] && m_va48[i] && m_va18[i]) {
            memcpy(&m_obj[0x0ad8 + i * 0xe0], m_va78[i], std::min<size_t>(nLL * 32, 0xe0));
            memcpy(&m_obj[0x1558 + i * 0xe0], m_vaa8[i], std::min<size_t>(nRL * 32, 0xe0));
        }
        if (!m_v958[i] || !m_v9b8[i] || !m_v9e8[i] || !m_va48[i] || !m_va18[i]) {
            err = "fish mesh incomplete: " + m_chosen[i];
            return false;
        }
        m_v988[i].assign(m_v958[i], m_v958[i] + nBody);
        for (int k = 0; k < 7; k++) m_frame[i][k] = nullptr;
        if (FTYPE(i) == 2) {
            memset(&m_obj[0x59c8 + i * 0xf0], 0, 0xf0);
            memset(&m_obj[0x6508 + i * 0xf0], 0, 0xf0);
            PHASE(i) = 0;
            GF(0x42d1c8 + i * 12) = VF(m_v958[i], 0x1020);
            GF(0x42d1cc + i * 12) = VF(m_v958[i], 0x7c4);
        } else {
            GF(0x42d1c8 + i * 12) = VF(m_v958[i], 0x2e0);
            GF(0x42d1cc + i * 12) = VF(m_v958[i], 0xc64);
        }
        if (XObject* o = f.Find("Wholefish")) { m_m7108[i] = &o->mat; m_frame[i][0] = o; }
        if (XObject* o = f.Find("Frame_fishbody")) m_frame[i][1] = o;
        if (XObject* o = f.Find("Frame_leftfin")) { m_m7138[i] = &o->mat; m_frame[i][3] = o; }
        if (XObject* o = f.Find("Frame_rightfin")) { m_m7168[i] = &o->mat; m_frame[i][4] = o; }
        if (XObject* o = f.Find("Frame_eyes")) { m_m7198[i] = &o->mat; m_frame[i][2] = o; }
        if (FTYPE(i) != 1) {
            if (XObject* o = f.Find("Frame_leftlowfin")) m_frame[i][5] = o;
            if (XObject* o = f.Find("Frame_rightlowfin")) m_frame[i][6] = o;
        }
        if (!m_m7108[i] || !m_m7138[i] || !m_m7168[i] || !m_m7198[i] || !m_frame[i][1]) {
            err = "fish frames incomplete: " + m_chosen[i];
            return false;
        }
        if (GB(0x430658)) f.ComputeNormals();
    }

    // starfish
    m_starfish = std::make_unique<XFile>();
    const std::vector<uint8_t>* sd = m_res->Find("10", "STARFISH.X");
    std::string e;
    if (!sd || !m_starfish->Load(*sd, e)) { err = "cannot load Starfish.x"; return false; }
    if (XObject* o = m_starfish->Find("Wholefish")) m_starMat = &o->mat;
    static const char* arms[5] = {"arm1", "arm2", "arm3", "arm4", "arm5"};
    for (int a = 0; a < 5; a++) m_arm[a] = m_starfish->GetMeshVertices(arms[a], nullptr);
    m_starshad = m_starfish->GetMeshVertices("starshad", nullptr);
    if (!m_starMat || !m_arm[0] || !m_arm[1] || !m_arm[2] || !m_arm[3] || !m_arm[4] || !m_starshad) {
        err = "starfish mesh incomplete";
        return false;
    }
    memcpy(&m_obj[0x414], m_arm[0], 0x150 * 4);

    const std::vector<uint8_t>* floor = m_res->Find("10", "FLOORDATA");
    if (!floor || floor->size() < 320 * 24) { err = "FLOORDATA missing"; return false; }
    m_floor = floor->data();
    return true;
}

// ============================================================================
// 0x404db0  OneTimeSceneInit
bool Aquarium::OneTimeSceneInit(std::string& err)
{
    if (!LoadFish(err)) return false;

    for (int i = 0; i < GI(0x427ddc); i++) {
        int dir;
        if (i < 4) { dir = 0; RY(i) = 62834.98f; }
        else { dir = 3; RY(i) = 62831.84f; }
        RZ(i) = -0.1f;
        OF(0x535c + i * 4) = rand_() * 6.103702e-05f;
        OF(0x538c + i * 4) = rand_() * 6.103702e-05f;
        if (FTYPE(i) == 2) {
            OF(0x535c + i * 4) *= 0.2f;
            OF(0x538c + i * 4) *= 0.2f;
        }
        OF(0x53ec + i * 4) = 0;
        OF(0x53bc + i * 4) = 0;
        OF(0x547c + i * 4) = 0;
        OI(0x52d8 + i * 4) = dir;
        OB(0x5320 + i) = 0;
        OS(0x5308 + i * 2) = 1;
        float z = dir != 0 ? 0.8f : -0.8f;
        float y = rand_() * 4.0f * 3.051851e-05f - 1.5f;
        float x = rand_() * 10.0f * 3.051851e-05f - 5.0f;
        PX(i) = x; PY(i) = y; PZ(i) = z;
        OF(0x55cc + i * 4) = 0;
        OF(0x55fc + i * 4) = 0;
        OI(0x7078 + i * 4) = 0;
        OI(0x70d8 + i * 4) = 1;
        PHASE(i) = 0;
        OF(0x565c + i * 4) = 0.5f;
        OF(0x568c + i * 4) = 0.5f;
        OB(0x595c + i) = 1;
        OF(0x58fc + i * 4) = 0;
        OF(0x592c + i * 4) = 0.01f;
        GU(0x42eebc + i * 4) = 0;
        OF(0x5998 + i * 4) = 0.01f;
        OF(0x5968 + i * 4) = 0;
        OI(0x7048 + i * 4) = 0x3b;
        OF(0x559c + i * 4) = GF(0x427de8);
        OF(0x556c + i * 4) = GF(0x427de4);
        GI(0x42d078 + i * 4) = 0;
        memset(&m_obj[0x56bc], 0, 0x78 * 4);           // tail-wave history rings (0x15af loop)
    }

    // lightplay meshes
    m_lightplaydist = std::make_unique<XFile>();
    m_lightplay = std::make_unique<XFile>();
    const std::vector<uint8_t>* a = m_res->Find("10", "LIGHTPLAYDIST.X");
    const std::vector<uint8_t>* b = m_res->Find("10", "LIGHTPLAY.X");
    std::string e;
    if (!a || !b || !m_lightplaydist->Load(*a, e) || !m_lightplay->Load(*b, e)) { err = "cannot load lightplay meshes"; return false; }
    m_lpdVerts = m_lightplaydist->GetMeshVertices("lightplaydist", &m_lpdCount);
    m_lpVerts = m_lightplay->GetMeshVertices("lightplay", &m_lpCount);
    if (!m_lpdVerts || !m_lpVerts) { err = "lightplay mesh missing"; return false; }

    // background gradient quad (D3DTLVERTEX at 0xa8)
    const float W = (float)m_r->Width(), H = (float)m_r->Height();
    GF(0x42f0d0) = W;
    GF(0x42f0d4) = H;
    auto tl = [&](size_t off, float sx, float sy, float sz, uint32_t col) {
        TLVertex& v = O(TLVertex, off);
        v.sx = sx; v.sy = sy; v.sz = sz; v.rhw = 0.9f; v.color = col; v.specular = 0; v.tu = 0; v.tv = 0;
    };
    tl(0xa8, 0, H * 0.7f, 0.01f, 0xff000000);
    tl(0xc8, 0, 0, 0.01f, 0xff000000);
    tl(0xe8, W, H * 0.7f, 0.01f, 0xff000000);
    tl(0x108, W, 0, 0.01f, 0xff000000);

    // bubble columns (D3DVERTEX) at 0x7278 and 0x72f8
    auto bv = [&](size_t off, float x, float y, float z, float tu, float tv) {
        D3DVertex& v = O(D3DVertex, off);
        v.x = x; v.y = y; v.z = z; v.nx = v.ny = v.nz = 0; v.tu = tu; v.tv = tv;
    };
    bv(0x7278, -2.0f, -2.7f, 1.2f, 0.0f, 0.0f);
    bv(0x7298, -2.3f, 5.7f, 1.2f, 0.0f, 1.8f);
    bv(0x72b8, -1.3f, -2.7f, 1.2f, 0.5f, 0.0f);
    bv(0x72d8, -1.0f, 5.7f, 1.2f, 0.5f, 1.8f);
    bv(0x72f8, -2.5f, -2.7f, 1.21f, 0.5f, 0.0f);
    bv(0x7318, -2.6f, 5.7f, 1.21f, 0.5f, 2.5f);
    bv(0x7338, -1.0f, -2.7f, 1.21f, 1.0f, 0.0f);
    bv(0x7358, -0.8f, 5.7f, 1.21f, 1.0f, 2.5f);
    // 12 single bubbles at 0x7378, 4 vertices each (stride 0x80)
    float f = 0.0f;
    for (int k = 0; k < 12; k++) {
        size_t o = 0x7378 + k * 0x80;
        bv(o + 0x00, -1.4f, f - 2.3f, 1.19f, 0.075f, 0.90625f);
        bv(o + 0x20, -1.4f, f - 2.2f, 1.19f, 0.075f, 0.93f);
        bv(o + 0x40, -1.3f, f - 2.3f, 1.19f, 0.125f, 0.90625f);
        bv(o + 0x60, -1.3f, f - 2.2f, 1.19f, 0.125f, 0.93f);
        f += 0.6f;
    }

    m_r->Texture("bubbles.dds");
    m_tankdist = std::make_unique<XFile>();
    m_bkgrnd = std::make_unique<XFile>();
    const std::vector<uint8_t>* td = m_res->Find("10", "TANKDISTANCE.X");
    const std::vector<uint8_t>* bg = m_res->Find("10", "BKGRND.X");
    if (!td || !m_tankdist->Load(*td, e)) { err = "cannot load tankdistance.x"; return false; }
    if (!bg || !m_bkgrnd->Load(*bg, e)) { err = "cannot load bkgrnd.x"; return false; }
    if (GB(0x430658)) {
        m_bkgrnd->ComputeNormals();
        m_starfish->ComputeNormals();
    }
    return true;
}

// ============================================================================
// 0x40b8d0  InitDeviceObjects: light, view, projection
void Aquarium::InitDeviceObjects()
{
    // D3DLIGHT2 at 0x42f120: parallel point at (0, 7, -1.3)
    GF(0x42f138) = 0.0f; GF(0x42f13c) = 7.0f; GF(0x42f140) = -1.3f;
    ApplyLight();

    Matrix view;
    MatView(view, Vec3{0.0f, 0.02f, -10.0f}, Vec3{0.0f, 0.02f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    OM(0x22c) = view;
    Matrix proj;
    MatProj(proj, 0.9305f, 0.75f, 1.0f, 15.0f);
    m_r->SetProj(proj);
    m_r->SetView(view);
}

// 0x402e10  light colour = foreground level, ambient = level * 150 / 255
void Aquarium::ApplyLight()
{
    float pos[3] = {GF(0x42f138), GF(0x42f13c), GF(0x42f140)};
    float col[3] = {GF(0x430374), GF(0x430378), GF(0x43037c)};
    m_r->SetLight(pos, col);
    int ar = (int)(GF(0x430374) * 150.0f), ag = (int)(GF(0x430378) * 150.0f), ab = (int)(GF(0x43037c) * 150.0f);
    float amb[3] = {ar / 255.0f, ag / 255.0f, ab / 255.0f};
    m_r->SetAmbient(amb);
}

// ============================================================================
// 0x4059b0  body bend: lerp base pose towards bend pose
void Aquarium::BendVertices(D3DVertex* out, const D3DVertex* bend, const D3DVertex* base,
                            uint32_t n, float x, float y)
{
    if (!out || !bend || !base) return;
    if (1.0f < y) y = 1.0f;
    float ry = 1.0f - y;
    for (uint32_t k = 0; k < n; k++) {
        out[k].x = (bend[k].x - base[k].x) * fabsf(x) + base[k].x;
        out[k].y = y * base[k].y + ry * bend[k].y;
        out[k].z = (bend[k].z - base[k].z) * x + base[k].z;
    }
}

// fin pivot helper: m = Tneg(-px,-py,-pz) * m
static void PivotBack(Matrix* m, Matrix& tmp, float px, float py, float pz)
{
    tmp.Identity();
    tmp.m[3][0] = -px; tmp.m[3][1] = -py; tmp.m[3][2] = -pz;
    MatMul(*m, tmp, *m);
}

// ============================================================================
// 0x4072a0  per-fish update
void Aquarium::FishMove(uint32_t t, int i)
{
    const int i4 = i * 4;
    float f3, f4;

    if (i == 0) GF(0x42eef0) = t * 0.002f;

    // keep inside the vertical range
    f4 = PY(i) - (PZ(i) * 0.5f + 4.0f);
    GF(0x42d000 + i4) = f4;
    if (0.0f < f4) { PY(i) = PY(i) - f4 * 0.1f; OI(0x70d8 + i4) = -1; }
    f3 = PY(i) + 2.5f;
    GF(0x42d048 + i4) = f3;
    if (f3 < 0.0f) { PY(i) = PY(i) - f3 * 0.1f; OI(0x70d8 + i4) = 1; }

    // speed
    if (FTYPE(i) == 2 && 0.5f < OF(0x535c + i4)) OF(0x535c + i4) = 0.5f;
    OF(0x532c + i4) = DT * OF(0x535c + i4) * 0.001f;
    if (OF(0x535c + i4) < OF(0x538c + i4)) OF(0x535c + i4) = DT * 0.0001f + OF(0x535c + i4);
    if (OF(0x538c + i4) < OF(0x535c + i4)) OF(0x535c + i4) = OF(0x535c + i4) - DT * 0.0001f;
    if (OU(0x53ec + i4) <= t) {
        OU(0x53ec + i4) = (uint32_t)((rand_() * 8000) / 0x7fff + 1000) + t;
        f3 = (float)(rand_() * 5.49333171e-05 + 0.001f);
        OF(0x538c + i4) = f3;
        int iv;
        bool b;
        if (FTYPE(i) == 2) {
            OF(0x538c + i4) = f3 * 0.1f;
            iv = OI(0x52d8 + i4);
            b = iv == 0;
        } else if (0.5f <= f3) {
            iv = OI(0x52d8 + i4);
            b = iv == 0;
        } else {
            iv = OI(0x52d8 + i4);
            b = false;
            if (iv == 0) {
                OF(0x538c + i4) = f3 * 0.2f;
                iv = OI(0x52d8 + i4);
                b = iv == 0;
            }
        }
        if (!b && -1 < iv && OF(0x538c + i4) < 0.5f) {
            OU(0x53ec + i4) = t + 1000;
            OF(0x538c + i4) = 0.5f;
            if (FTYPE(i) == 2) OF(0x538c + i4) = 0.3f;
        }
    }

    // heading sector (16 x 22.5 degrees) for part draw order
    float ang = (float)fmod((double)RY(i), 6.283184051513672);
    GF(0x42d090 + i4) = ang;
    int sector = 0;
    do {
        if (sector * 0.392699f + 0.1963495f < ang && ang < (sector + 1) * 0.392699f + 0.1963495f) break;
        sector++;
    } while (sector < 0xf);
    const double a = GF(0x42d090 + i4);

    // lane limits per state
    auto laneTurn = [&]() {
        if (OF(0x553c + i4) < PZ(i)) {
            if (1.57295 < a) {
                if (a < 3.1416) { OF(0x568c + i4) = 0.9f; OF(0x565c + i4) = 0.6f; OI(0x70a8 + i4) = 1; }
            } else { OF(0x568c + i4) = -0.6f; OF(0x565c + i4) = -0.9f; OI(0x70a8 + i4) = -1; }
        }
        if (PZ(i) < OF(0x550c + i4)) {
            if (a <= 3.14 || 4.7124 < a) {
                if (4.7124 < a) { OF(0x568c + i4) = 0.8f; OF(0x565c + i4) = 0.7f; OI(0x70a8 + i4) = 1; }
            } else { OF(0x568c + i4) = -0.7f; OF(0x565c + i4) = -0.8f; OI(0x70a8 + i4) = -1; }
        }
    };
    if (OI(0x52d8 + i4) == 0) {
        GI(0x42d078 + i4) = 0;
        OF(0x54ac + i4) = -1.0f; OF(0x54dc + i4) = -0.7f; OF(0x553c + i4) = -0.8f; OF(0x550c + i4) = -0.8f;
        laneTurn();
    }
    if (OI(0x52d8 + i4) == 3) {
        GI(0x42d078 + i4) = 0;
        OF(0x54dc + i4) = 1.3f; OF(0x54ac + i4) = 0.6f; OF(0x553c + i4) = 1.1f; OF(0x550c + i4) = 0.8f;
        laneTurn();
    }

    // turn around at the tank ends
    if (OF(0x559c + i4) <= PX(i) || (1.58 <= a && a <= 4.7)) {
        if (OF(0x556c + i4) < PX(i) && 1.58 < a && a < 4.7) {
            if (FTYPE(i) == 2 && OF(0x538c + i4) < 0.4f) { OF(0x538c + i4) = 0.4f; OU(0x53ec + i4) = t + 2000; }
            if (OI(0x52d8 + i4) < 2) { OI(0x70a8 + i4) = -1; OF(0x568c + i4) = -0.4f; OF(0x565c + i4) = -0.8f; }
            if (1 < OI(0x52d8 + i4)) { OI(0x70a8 + i4) = 1; OF(0x568c + i4) = 0.8f; OF(0x565c + i4) = 0.4f; }
            OF(0x562c + i4) = -0.5f;
            if (OS(0x5308 + i * 2) != 0 || PY(i) <= -0.5f) {
                OF(0x559c + i4) = GF(0x427de8);
            } else {
                if (OB(0x5320 + i) == 0 || OF(0x556c + i4) != 0.0f) {
                    OF(0x556c + i4) = GF(0x427dec);
                    OF(0x559c + i4) = GF(0x427dec) - 0.4f;
                    if (FTYPE(i) == 2) { OF(0x556c + i4) = GF(0x427dec); OF(0x559c + i4) = GF(0x427dec) - 0.2f; }
                } else if (FTYPE(i) == 2) { OF(0x559c + i4) = -0.1f; OF(0x556c + i4) = 0.0f; }
                else { OF(0x559c + i4) = -0.2f; OF(0x556c + i4) = 0.1f; }
                OF(0x54dc + i4) = 1.3f;
                OF(0x54ac + i4) = -1.0f;
                if (OI(0x52d8 + i4) == 0) { OI(0x52d8 + i4) = 1; if (4.0f < PX(i)) GI(0x42d078 + i4) = 1; }
                if (OI(0x52d8 + i4) == 3) { OI(0x52d8 + i4) = 2; if (4.0f < PX(i)) GI(0x42d078 + i4) = 2; }
                OS(0x5308 + i * 2) = 2;
            }
        }
    } else {
        if (FTYPE(i) == 2 && OF(0x538c + i4) < 0.4f) { OF(0x538c + i4) = 0.4f; OU(0x53ec + i4) = t + 2000; }
        if (OI(0x52d8 + i4) < 2) { OI(0x70a8 + i4) = 1; OF(0x568c + i4) = 0.6f; OF(0x565c + i4) = 0.2f; }
        if (1 < OI(0x52d8 + i4)) { OI(0x70a8 + i4) = -1; OF(0x568c + i4) = -0.2f; OF(0x565c + i4) = -0.6f; }
        OF(0x562c + i4) = (PX(i) <= -4.0f) ? 1.0f : -0.5f;
        if (OS(0x5308 + i * 2) != 0 || PY(i) <= OF(0x562c + i4)) {
            OF(0x556c + i4) = GF(0x427de4);
        } else {
            if (OB(0x5320 + i) == 0 || OI(0x559c + i4) != (int32_t)0xbdcccccd) {
                OF(0x559c + i4) = -GF(0x427dec);
                OF(0x556c + i4) = 0.4f - GF(0x427dec);
                if (FTYPE(i) == 2) { OF(0x559c + i4) = -GF(0x427dec) - 0.2f; OF(0x556c + i4) = -GF(0x427dec); }
            } else if (FTYPE(i) == 2) { OF(0x559c + i4) = -0.1f; OF(0x556c + i4) = 0.0f; }
            else { OF(0x559c + i4) = -0.3f; OF(0x556c + i4) = 0.1f; }
            OF(0x54dc + i4) = 1.3f;
            OF(0x54ac + i4) = -1.0f;
            if (OI(0x52d8 + i4) == 0) { OI(0x52d8 + i4) = 1; if (PX(i) < -4.0f) GI(0x42d078 + i4) = 2; }
            if (OI(0x52d8 + i4) == 3) { OI(0x52d8 + i4) = 2; if (PX(i) < -4.0f) GI(0x42d078 + i4) = 1; }
            OS(0x5308 + i * 2) = 2;
        }
    }

    // finished a turn: settle into the other lane
    const float lim = GF(0x427de0);
    if (OI(0x52d8 + i4) == 1 && 0.8 <= PZ(i)) {
        OI(0x52d8 + i4) = 3;
        if (OB(0x5320 + i) != 0) {
            OS(0x5308 + i * 2) = 2; OB(0x5320 + i) = 0;
            OF(0x556c + i4) = GF(0x427de4); OF(0x559c + i4) = GF(0x427de8);
        }
        if (PX(i) < -lim) {
            OF(0x568c + i4) = 0.9f; OF(0x565c + i4) = 0.6f; OI(0x70a8 + i4) = 1;
            if (8 < (rand_() * 0x10) / 0x7fff) { OB(0x5320 + i) = 1; OF(0x556c + i4) = 0.0f; }
        }
        if (lim < PX(i)) {
            OF(0x568c + i4) = -0.6f; OF(0x565c + i4) = -0.9f; OI(0x70a8 + i4) = -1;
            if (8 < (rand_() * 0x10) / 0x7fff) { OB(0x5320 + i) = 1; OF(0x559c + i4) = -0.1f; }
        }
    }
    if (OI(0x52d8 + i4) == 2 && PZ(i) < -0.8) {
        OI(0x52d8 + i4) = 0;
        if (OB(0x5320 + i) != 0) {
            OS(0x5308 + i * 2) = 2; OB(0x5320 + i) = 0;
            OF(0x556c + i4) = GF(0x427de4); OF(0x559c + i4) = GF(0x427de8);
        }
        if (lim < PX(i)) {
            OF(0x568c + i4) = 0.9f; OF(0x565c + i4) = 0.6f; OI(0x70a8 + i4) = 1;
            if (8 < (rand_() * 0x10) / 0x7fff) { OB(0x5320 + i) = 1; OF(0x559c + i4) = -0.1f; }
        }
        if (PX(i) < -lim) {
            OF(0x568c + i4) = -0.6f; OF(0x565c + i4) = -0.9f; OI(0x70a8 + i4) = -1;
            if (8 < (rand_() * 0x10) / 0x7fff) { OB(0x5320 + i) = 1; OF(0x556c + i4) = 0.0f; }
        }
    }

    // drift back into the x window
    float dt = DT;
    if (PX(i) < OF(0x559c + i4) - 0.1f) PX(i) = dt * 0.0001f + PX(i);
    if (OF(0x556c + i4) + 0.1f < PX(i)) PX(i) = PX(i) - dt * 0.0001f;
    int st = OI(0x52d8 + i4);
    if ((st == 2 || st == 1) && PY(i) < OF(0x562c + i4)) PY(i) = OF(0x562c + i4);
    if (OS(0x5308 + i * 2) == 2 &&
        ((1.0f - GF(0x427de8) < PX(i) && PX(i) < -3.0f) || (3.0f < PX(i) && PX(i) < GF(0x427de4) - 1.0f)))
        OS(0x5308 + i * 2) = 0;

    // avoidance against every other fish
    for (int j = 0; j < GI(0x42ce38); j++) {
        GI(0x42d040) = j;
        if (j == i || FISHON(j) == 0) continue;
        if (OS(0x5308 + i * 2) == 1) OS(0x5308 + i * 2) = 0;
        float dz = PZ(i) - PZ(j), dy = PY(i) - PY(j), dx = PX(i) - PX(j);
        float d2 = dz * dz + dy * dy + dx * dx;
        if (d2 < 16.0f) { OS(0x5308 + i * 2) = 1; OS(0x5308 + j * 2) = 1; }
        int si = OI(0x52d8 + i4), sj = OI(0x52d8 + j * 4);
        if (((si != 0 || sj != 3) && (si != 3 || sj != 0)) && d2 <= 4.0f) {
            float d = sqrtf(d2);
            if (1.0f < OF(0x538c + i4)) { OF(0x538c + i4) = 1.0f; OU(0x53ec + i4) = t + 1000; }
            if (OF(0x538c + i4) < 0.3f) { OF(0x538c + i4) = 0.3f; OU(0x53ec + i4) = t + 1000; }
            float sx = GF(0x42d1c8 + j * 12) + GF(0x42d1c8 + i * 12);
            float inv = 1.0f / (d * d * d);
            GF(0x42eab8 + i4) = OF(0x532c + i4) * inv * sx;
            float sy = GF(0x42d1cc + j * 12) + GF(0x42d1cc + i * 12);
            float py = OF(0x532c + i4) * sy * inv;
            GF(0x42eae8 + i4) = py + py;
            if (0.0f <= dx) {
                if (1.58f < a && a < 4.71f) GF(0x42eab8 + i4) = GF(0x42eab8 + i4) * 0.5f;
                float push = GF(0x42eab8 + i4), x0 = PX(i);
                PX(i) = push + x0;
                if (dx < sx && (OI(0x52d8 + i4) == 0 || OI(0x52d8 + i4) == 3) &&
                    GF(0x42d1c8 + i * 12) <= GF(0x42d1c8 + j * 12) && fabsf(dy) < sy + 0.5f) {
                    OF(0x559c + i4) = push + x0;
                    OF(0x556c + i4) = GF(0x427de4);
                }
            } else {
                if (a < 1.56f || 4.73f < a) GF(0x42eab8 + i4) = GF(0x42eab8 + i4) * 0.5f;
                float x0 = PX(i), push = GF(0x42eab8 + i4);
                PX(i) = x0 - push;
                if (-sx < dx && (OI(0x52d8 + i4) == 0 || OI(0x52d8 + i4) == 3) &&
                    GF(0x42d1c8 + i * 12) <= GF(0x42d1c8 + j * 12) && fabsf(dy) < sy + 0.5f) {
                    OF(0x556c + i4) = x0 - push;
                    OF(0x559c + i4) = GF(0x427de8);
                }
            }
            if (0.0f <= dy) { OF(0x544c + i4) = 4.0f; PY(i) = PY(i) + GF(0x42eae8 + i4); }
            else { OF(0x544c + i4) = -2.5f; PY(i) = PY(i) - GF(0x42eae8 + i4); }
        }
    }

    // tail oscillator
    float* ph = &PHASE(i);
    f3 = OF(0x532c + i4);
    f3 = f3 + f3;
    if (OI(0x70a8 + i4) == 1) {
        f3 = f3 + *ph;
        *ph = f3;
        if (OF(0x568c + i4) < f3) {
            float s = OF(0x532c + i4);
            *ph = f3 - (s + s);
            OI(0x70a8 + i4) = -1;
            f3 = rand_() * GF(0x427df0) * -3.051851e-05f;
            OF(0x565c + i4) = f3;
            if (f3 < -1.0f) OF(0x565c + i4) = -0.5f;
        }
    } else {
        f3 = *ph - f3;
        *ph = f3;
        if (f3 < OF(0x565c + i4)) {
            float s = OF(0x532c + i4);
            *ph = s + s + f3;
            OI(0x70a8 + i4) = 1;
            f3 = rand_() * GF(0x427df0) * 3.051851e-05f;
            OF(0x568c + i4) = f3;
            if (1.0f < f3) OF(0x568c + i4) = 0.5f;
        }
    }
    BendVertices(m_v958[i], m_v9b8[i], m_v988[i].data(), (uint32_t)OI(0x7248 + i4), -*ph, OF(0x535c + i4) - 0.1f);
    if (FTYPE(i) == 2) {
        float s = OF(0x535c + i4);
        GF(0x42cd64) = 1.0f - (s + s);
        GF(0x42f0d8) = s + s;
        const float wb = GF(0x42f0d8), wn = GF(0x42cd64);
        D3DVertex* b = m_v958[i];
        const D3DVertex* r = m_v988[i].data();
        const D3DVertex* n = m_v9b8[i];
        for (int off : {0x0, 0x20, 0x7c0}) VF(b, off) = wb * VF((void*)r, off) + wn * VF((void*)n, off);
        for (int off : {0x1440, 0x1520}) VF(b, off) = wn * VF((void*)n, off) + wb * VF((void*)r, off);
    }

    // vertical target
    if (OU(0x547c + i4) < t) {
        OU(0x547c + i4) = (uint32_t)((rand_() * 8000) / 0x7fff + 1000) + t;
        OF(0x544c + i4) = (float)(rand_() * 0.000106814783 - 1.0f);
    }
    if (PY(i) < OF(0x544c + i4)) OI(0x70d8 + i4) = 1;
    if (OF(0x544c + i4) < PY(i)) OI(0x70d8 + i4) = -1;
    if (OI(0x70d8 + i4) == 1 && OF(0x55cc + i4) < 0.1) OF(0x55cc + i4) = OF(0x55cc + i4) + 0.01f;
    if (OI(0x70d8 + i4) == -1 && -0.1 < OF(0x55cc + i4)) OF(0x55cc + i4) = OF(0x55cc + i4) - 0.01f;

    // heading / pitch and position
    RX(i) = 0;
    f3 = OF(0x532c + i4) * *ph;
    RY(i) = f3 + f3 + RY(i);
    f3 = RZ(i) - OF(0x55cc + i4) * OF(0x532c + i4);
    RZ(i) = f3;
    if (0.5 < f3) RZ(i) = 0.5f;
    if (RZ(i) < -0.5) RZ(i) = -0.5f;
    Matrix& mz = GM(0x42ce40);
    Matrix& my = GM(0x430290);
    Matrix& mt = GM(0x430050);
    MatRotZ(mz, RZ(i));
    MatRotY(my, RY(i));
    MatMul(mt, mz, my);
    MatMul(mt, OM(0x7208), mt);
    PX(i) = mt.m[3][0] * OF(0x532c + i4) + PX(i);
    PY(i) = mt.m[3][1] * OF(0x532c + i4) + PY(i);
    f3 = mt.m[3][2] * OF(0x532c + i4) + PZ(i);
    PZ(i) = f3;
    if (OF(0x54dc + i4) < f3) PZ(i) = OF(0x54dc + i4);
    if (PZ(i) < OF(0x54ac + i4)) PZ(i) = OF(0x54ac + i4);

    Matrix* whole = m_m7108[i];
    SetTranslation(whole, PX(i), PY(i), PZ(i));
    MatMul(*whole, my, *whole);
    MatMul(*whole, mz, *whole);

    // fins
    int type = FTYPE(i);
    if (type == 2) {
        // lionfish: fan fins rotated per vertex with a delay line
        if (GI(0x42d078 + i4) == 1) {
            if (GF(0x43025c + i4) < 1.2f) GF(0x43025c + i4) = GF(0x43025c + i4) + 0.01f;
        } else if (GI(0x42d078 + i4) == 2) {
            if (GF(0x42ce80 + i4) < 1.2f) GF(0x42ce80 + i4) = GF(0x42ce80 + i4) + 0.01f;
        } else {
            if (0.0f < GF(0x42ce80 + i4)) GF(0x42ce80 + i4) = GF(0x42ce80 + i4) - 0.01f;
            if (0.0f < GF(0x43025c + i4)) GF(0x43025c + i4) = GF(0x43025c + i4) - 0.01f;
        }
        const int base = i * 0x3c;
        auto L = [&](int arr, int back) -> float& { return OF(arr + (OI(0x7048 + i4) + base) * 4 - back); };
        int idx = OI(0x7048 + i4) + base;
        OF(0x59c8 + idx * 4) = (1.2f - GF(0x43025c + i4)) * *ph - GF(0x43025c + i4) * 0.5f;
        OF(0x5950 + idx * 4) = OF(0x59c8 + idx * 4);
        Matrix& ml = GM(0x4302d0);
        Matrix& mr = GM(0x42cd20);
        MatRotY(ml, OF(0x59c8 + idx * 4));
        OF(0x6508 + idx * 4) = GF(0x42ce80 + i4) * 0.5f + (1.2f - GF(0x42ce80 + i4)) * *ph;
        OF(0x6490 + idx * 4) = OF(0x6508 + idx * 4);
        MatRotY(mr, OF(0x6508 + idx * 4));
        uint8_t* lb = &m_obj[i * 0x220 + 0x1fd8];
        uint8_t* rb = &m_obj[i * 0x220 + 0x3958];
        D3DVertex* lf = m_va18[i];
        D3DVertex* rf = m_va48[i];
        auto XL = [&](int vo, int bo) { XformVertex(VP(lf, vo), lb + bo, ml); };
        auto XR = [&](int vo, int bo) { XformVertex(VP(rf, vo), rb + bo, mr); };
        XL(0x40, 0x40); XR(0x40, 0x40); XL(0x160, 0x160); XR(0x160, 0x160);
        MatRotY(ml, L(0x59c8, 0xc)); MatRotY(mr, L(0x6508, 0xc));
        XL(0x20, 0x20); XR(0x20, 0x20);
        MatRotY(ml, L(0x59c8, 0x20)); MatRotY(mr, L(0x6508, 0x20));
        XL(0x0, 0x0); XR(0x0, 0x0);
        MatRotY(ml, L(0x59c8, 0x3c)); MatRotY(mr, L(0x6508, 0x3c));
        XL(0xa0, 0xa0); XR(0xa0, 0xa0); XL(0x140, 0x140); XR(0x140, 0x140);
        MatRotY(ml, L(0x59c8, 0x48)); MatRotY(mr, L(0x6508, 0x48));
        XL(0x80, 0x80); XR(0x80, 0x80); XL(0x120, 0x120); XR(0x120, 0x120);
        MatRotY(ml, L(0x59c8, 0x58)); MatRotY(mr, L(0x6508, 0x58));
        XL(0x60, 0x60); XR(0x60, 0x60); XL(0x100, 0x100); XR(0x100, 0x100);
        MatRotY(ml, L(0x59c8, 0x64)); MatRotY(mr, L(0x6508, 0x64));
        XL(0xe0, 0xe0); XR(0xe0, 0xe0);
        MatRotY(ml, L(0x59c8, 0x74)); MatRotY(mr, L(0x6508, 0x74));
        XL(0xc0, 0xc0); XR(0xc0, 0xc0);
        int n = OI(0x7048 + i4) + 1;
        OI(0x7048 + i4) = n;
        if (0x3b < n) OI(0x7048 + i4) = 0x1e;
    } else {
        Matrix* lm = m_m7138[i];
        Matrix* rm = m_m7168[i];
        D3DVertex* lf = m_va18[i];
        D3DVertex* rf = m_va48[i];
        Matrix& tn = GM(0x430050);
        if (type == 1) {
            SetTranslation(lm, lf[0].x, lf[0].y, VF(m_v958[i], 0x9c8));
            MatRotZ(mz, *ph * -0.3333333f);
            MatRotY(my, *ph + 0.3f);
            MatMul(*lm, my, *lm);
            MatMul(*lm, mz, *lm);
            PivotBack(lm, tn, lf[0].x, lf[0].y, -0.1f);   // translate z by +0.1 (0x430088)
            SetTranslation(rm, rf[0].x, rf[0].y, VF(m_v958[i], 0x13a8));
            MatRotY(my, *ph - 0.3f);
            MatMul(*rm, my, *rm);
            MatMul(*rm, mz, *rm);
            PivotBack(rm, tn, rf[0].x, rf[0].y, 0.1f);    // translate z by -0.1
        } else if (type == 3) {
            SetTranslation(lm, lf[0].x, lf[0].y, VF(m_v958[i], 0x9c8));
            MatRotZ(mz, *ph * -0.25f);
            MatRotY(my, *ph * 0.65f + 0.3f);
            MatMul(*lm, my, *lm);
            MatMul(*lm, mz, *lm);
            PivotBack(lm, tn, lf[0].x, lf[0].y, -0.1f);   // translate z by +0.1 (0x430088)
            SetTranslation(rm, rf[0].x, rf[0].y, VF(m_v958[i], 0x13a8));
            MatRotZ(mz, *ph * -0.3333333f);
            MatRotY(my, *ph * 0.65f - 0.4f);
            MatMul(*rm, my, *rm);
            MatMul(*rm, mz, *rm);
            PivotBack(rm, tn, rf[0].x, rf[0].y, 0.1f);    // translate z by -0.1
        } else if (type == 4) {
            SetTranslation(lm, lf[0].x, lf[0].y, lf[0].z);
            MatRotZ(mz, *ph * -0.3333333f);
            MatRotY(my, *ph + 0.3f);
            MatMul(*lm, my, *lm);
            MatMul(*lm, mz, *lm);
            PivotBack(lm, tn, lf[0].x, lf[0].y, -0.085f); // translate z by +0.085
            SetTranslation(rm, rf[0].x, rf[0].y, rf[0].z);
            MatRotZ(mz, *ph * -0.3333333f);
            MatRotY(my, *ph - 0.3f);
            MatMul(*rm, my, *rm);
            MatMul(*rm, mz, *rm);
            PivotBack(rm, tn, rf[0].x, rf[0].y, 0.085f);  // translate z by -0.085
            // low fins follow the body's z bend
            float dz = VF(m_v958[i], 0x988) - m_v988[i][0x988 / 32].z;
            for (int k = 0; k < 6; k++) {
                VF(m_va78[i], k * 0x20 + 8) = dz + OF(i * 0xe0 + 0x0ae0 + k * 0x20);
                VF(m_vaa8[i], k * 0x20 + 8) = dz + OF(i * 0xe0 + 0x1560 + k * 0x20);
            }
        } else {
            SetTranslation(lm, lf[0].x, lf[0].y, VF(m_v958[i], 0x9c8));
            MatRotZ(mz, *ph * -0.3333333f);
            MatRotY(my, *ph + 0.3f);
            MatMul(*lm, my, *lm);
            MatMul(*lm, mz, *lm);
            PivotBack(lm, tn, lf[0].x, lf[0].y, VF(m_v958[i], 0x9c8));
            SetTranslation(rm, rf[0].x, rf[0].y, VF(m_v958[i], 0x13a8));
            MatRotZ(mz, *ph * -0.3333333f);
            MatRotY(my, *ph - 0.3f);
            MatMul(*rm, my, *rm);
            MatMul(*rm, mz, *rm);
            PivotBack(rm, tn, rf[0].x, rf[0].y, VF(m_v958[i], 0x13a8));
        }
    }

    // eyes look around
    if (OU(0x53bc + i4) <= t) {
        OU(0x53bc + i4) = OU(0x53bc + i4) + (uint32_t)((rand_() * 3000) / 0x7fff + 1000);
        OF(0x541c + i4) = rand_() * 1.525925e-05f - 0.25f;
    }
    {
        D3DVertex* ev = m_v9e8[i];
        Matrix* em = m_m7198[i];
        SetTranslation(em, ev[1].x, ev[1].y, 0.0f);
        MatRotY(my, OF(0x541c + i4));
        MatMul(*em, my, *em);
        PivotBack(em, GM(0x430050), ev[1].x, ev[1].y, 0.0f);
    }

    // tail wave delay ring
    {
        int r = OI(0x7078 + i4);
        OI(0x7078 + i4) = r + 1;
        GI(0x4306c4) = r - 1;
        GI(0x4306c8) = OI(0x7078 + i4) - 4;
        if (OI(0x7078 + i4) == 0xc) OI(0x7078 + i4) = 0;
        if (GI(0x4306c4) < 0) GI(0x4306c4) += 0xc;
        if (GI(0x4306c8) < 0) GI(0x4306c8) += 0xc;
    }
    const int ring = OI(0x7078 + i4);
    const int hb = i * 0xc;
    D3DVertex* body = m_v958[i];
    const float tt = GF(0x42eef0);
    auto H = [&](int k) -> float& { return OF(0x56bc + (hb + k) * 4); };
    const int h1 = GI(0x4306c4), h2 = GI(0x4306c8);

    if (type == 2) {
        H(ring) = VF(body, 0x1068);
        float c = H(h1);
        c = c + c;
        GF(0x43010c) = c;
        VF(body, 0x1068) = cosf((tt + 13.0f) * 7.0f) * 0.05f + c;
        VF(body, 0x1048) = cosf((tt + 12.0f) * 7.0f) * 0.05f + c;
        VF(body, 0xf88) = cosf((tt + 10.0f) * 7.0f) * 0.05f + c;
        VF(body, 0xfa8) = cosf((tt + 8.0f) * 7.0f) * 0.05f + c;
        VF(body, 0x1028) = cosf((tt + 6.0f) * 7.0f) * 0.05f + c;
        VF(body, 0xfe8) = cosf((tt + 4.0f) * 7.0f) * 0.05f + c;
        VF(body, 0xee8) = cosf((tt + 2.0f) * 7.0f) * 0.05f + c;
        VF(body, 0xf08) = cosf(tt * 7.0f) * 0.05f + c;
        VF(body, 0xfc8) = cosf(tt * 7.0f) * 0.05f + c;
        VF(body, 0x8) = cosf((tt * 0.125f + 10.0f) * 7.0f) * 0.25f;
        VF(body, 0x28) = cosf((tt * 0.125f + 9.0f) * 7.0f) * 0.25f;
        VF(body, 0x7c8) = cosf((tt * 0.125f + 7.5f) * 7.0f) * 0.25f;
        VF(body, 0x1448) = cosf((tt * 0.125f + 5.0f) * 7.0f) * 0.25f;
        VF(body, 0x1528) = cosf((tt * 0.125f + 3.0f) * 7.0f) * 0.25f;
        const float d = H(h2);
        VF(body, 0x668) += d * 0.2f;
        VF(body, 0x6c8) += d * 0.3f;
        VF(body, 0x748) += d * 0.6f;
        VF(body, 0x788) += d * 0.6f;
        VF(body, 0x568) += d;
        VF(body, 0x4c8) += d;
        VF(body, 0x4a8) += d * 0.3f;
        VF(body, 0x5e8) += d * 0.18f;
        GF(0x42cdcc) = VF(body, 0x748) * -0.6f;
        const float lz = GF(0x42cdcc);
        const int lb = i * 0xe0;
        VF(m_va78[i], 0x08) = lz + OF(lb + 0xae0);
        VF(m_vaa8[i], 0x08) = lz + OF(lb + 0x1560);
        VF(m_va78[i], 0x68) = lz + OF(lb + 0xb40);
        VF(m_vaa8[i], 0x68) = lz + OF(lb + 0x15c0);
        VF(m_va78[i], 0x28) = lz * 0.7f + OF(lb + 0xb00);
        VF(m_vaa8[i], 0x28) = lz * 0.7f + OF(lb + 0x1580);
        VF(m_va78[i], 0x48) = lz * 0.6f + OF(lb + 0xb20);
        VF(m_vaa8[i], 0x48) = lz * 0.6f + OF(lb + 0x15a0);
        VF(m_va78[i], 0x88) = lz * 0.4f + OF((i + 0xd) * 0xe0);
        VF(m_vaa8[i], 0x88) = lz * 0.4f + OF((i + 0x19) * 0xe0);
        VF(body, 0x668) += cosf((tt + 13.0f) * 7.0f) * 0.08333334f;
        VF(body, 0x6c8) += cosf((tt + 10.0f) * 7.0f) * 0.08333334f;
        VF(body, 0x748) += cosf((tt + 7.0f) * 7.0f) * 0.0625f;
        VF(body, 0x788) += cosf((tt + 3.0f) * 7.0f) * 0.03125f;
        VF(body, 0x4a8) += cosf((tt + 13.0f) * 7.0f) * 0.0625f;
        VF(body, 0x4c8) += cosf((tt + 10.0f) * 7.0f) * 0.0625f;
        VF(body, 0x568) += cosf((tt + 7.0f) * 7.0f) * 0.0625f;

        // mouth gulp
        if (GU(0x42eebc + i4) < GU(0x4306a0)) OB(0x595c + i) = 1;
        if (OB(0x595c + i) != 0) {
            float m = OF(0x592c + i4) + OF(0x58fc + i4);
            OF(0x58fc + i4) = m;
            if (0.3f < m) OF(0x592c + i4) = -0.01f;
        }
        if (OF(0x58fc + i4) <= 0.0f) {
            OF(0x592c + i4) = 0.005f;
            GU(0x42eebc + i4) = (uint32_t)((rand_() * 2000) / 0x7fff) + GU(0x4306a0);
            OB(0x595c + i) = 0;
        }
        MatRotZ(OM(0x71c8), OF(0x58fc + i4));
        for (int k = 0; k < 0x14; k++) {
            GI(0x42d040) = k;
            float* v = reinterpret_cast<float*>(VP(body, GI(0x427ef4 + k * 4) * 0x20));
            v[0] -= VF(body, 0x240);
            v[1] -= VF(body, 0x244);
            VecMatMul(v, v, OM(0x71c8));
            v[0] += VF(body, 0x240);
            v[1] += VF(body, 0x244);
        }
        const float g = OF(0x58fc + i4);
        VF(body, 0x328) += g * 0.15f;
        VF(body, 0x3a8) -= g * 0.15f;
        VF(body, 0x308) += g * 0.15f;
        VF(body, 0x3c8) -= g * 0.15f;
        VF(body, 0x488) += g * 0.16f;
        VF(body, 0x448) -= g * 0.16f;
        VF(body, 0x320) -= g * 0.2f;
        VF(body, 0x3a0) -= g * 0.2f;
        VF(body, 0x300) -= g * 0.2f;
        VF(body, 0x3c0) -= g * 0.2f;
        VF(body, 0x260) -= g * 0.15f;
        VF(body, 0x280) -= g * 0.15f;
    } else {
        H(ring) = VF(body, 0x2c8);
        const float c = H(h1);
        GF(0x43010c) = c;
        const float k = 0.03571429f;
        VF(body, 0x2c8) = cosf((tt + 13.0f) * 7.0f) * k + c;
        VF(body, 0x208) = cosf((tt + 12.0f) * 7.0f) * k + c;
        VF(body, 0x1e8) = cosf((tt + 10.0f) * 7.0f) * k + c;
        VF(body, 0x2e8) = cosf((tt + 8.0f) * 7.0f) * k + c;
        VF(body, 0x328) = cosf((tt + 6.0f) * 7.0f) * k + c;
        VF(body, 0x2a8) = cosf((tt + 4.0f) * 7.0f) * k + c;
        VF(body, 0x288) = cosf((tt + 2.0f) * 7.0f) * k + c;
        VF(body, 0x368) = cosf(tt * 7.0f) * k + c;
        VF(body, 0x388) = cosf(tt * 7.0f) * k + c;
        const float d = H(h2);
        VF(body, 0x488) += d * 0.1666667f;
        VF(body, 0x4a8) += d * 0.25f;
        VF(body, 0x4e8) += d * 0.3333333f;
        VF(body, 0x568) += d * 0.3333333f;
        VF(body, 0x188) += d * 0.25f;
        VF(body, 0x1a8) += d * 0.25f;
        VF(body, 0xc8) += d * 0.25f;
        VF(body, 0x128) += d * 0.125f;
    }

    SetDrawOrder(i, sector);

    if (type == 1) {
        VF(body, 0x568) += cosf((tt + 13.0f) * 7.0f) * 0.08333334f;
        VF(body, 0x4e8) += cosf((tt + 10.0f) * 7.0f) * 0.08333334f;
        VF(body, 0x4a8) += cosf((tt + 7.0f) * 7.0f) * 0.0625f;
        VF(body, 0x488) += cosf((tt + 3.0f) * 7.0f) * 0.03125f;
        VF(body, 0xc8) += cosf((tt + 13.0f) * 7.0f) * 0.0625f;
        VF(body, 0x1a8) += cosf((tt + 10.0f) * 7.0f) * 0.0625f;
        VF(body, 0x188) += cosf((tt + 7.0f) * 7.0f) * 0.0625f;
    }
    if (GB(0x430658) != 0) m_fishFile[i]->ComputeNormals("fishbody");
}

// Tail of 0x4072a0: the original relinks the Wholefish child/sibling list so
// fins and body are drawn back-to-front for the current heading sector.
void Aquarium::SetDrawOrder(int i, int sector)
{
    XObject* A = m_frame[i][1];
    XObject* E = m_frame[i][2];
    XObject* L = m_frame[i][3];
    XObject* R = m_frame[i][4];
    XObject* LL = m_frame[i][5];
    XObject* RL = m_frame[i][6];
    XObject* W = m_frame[i][0];
    if (!W) return;
    const bool trig = FTYPE(i) == 1;
    const bool lion = FTYPE(i) == 2;
    std::vector<XObject*> o;
    if (lion) {
        if (sector <= 3 || sector >= 0xc) o = {R, RL, E, A, LL, L};
        else if (sector == 4) o = {L, LL, RL, R, A, E};
        else o = {L, LL, E, A, RL, R};
    } else if (sector <= 2 || sector >= 0xd) {
        o = trig ? std::vector<XObject*>{R, E, A, L} : std::vector<XObject*>{R, RL, E, A, LL, L};
    } else if (sector == 4) {
        o = trig ? std::vector<XObject*>{L, R, A, E} : std::vector<XObject*>{L, LL, RL, R, A, E};
    } else if (sector == 0xc) {
        o = trig ? std::vector<XObject*>{E, A, R, L} : std::vector<XObject*>{E, A, R, RL, LL, L};
    } else {
        o = trig ? std::vector<XObject*>{L, E, A, R} : std::vector<XObject*>{L, LL, E, A, RL, R};
    }
    W->order.clear();
    for (XObject* p : o) if (p) W->order.push_back(p);
}

// ============================================================================
// 0x4060b0  starfish: crawls on the floor (FLOORDATA height map) or the glass
static int Ftol(double v) { return (int)v; }

void Aquarium::StarfishMove(uint32_t t)
{
    if (!m_starfish) return;
    if (GB(0x427e4d) == 0) GI(0x4306a8) = 0;
    if (GB(0x430662) != 0) {
        GB(0x430662) = 0;
        if (GI(0x4306a8) == 0) {
            GF(0x42d26c) = GF(0x42d26c) + GF(0x424c34);
            GF(0x427e60) = 0.1f;
            GF(0x427e58) = 0.02f;
            int r = rand_();
            GI(0x4304b8) = 5;
            GF(0x430664) = 0.5f;
            GF(0x427f9c) = 0.6f;
            GF(0x427f88) = 0.2f;
            GU(0x430694) = (uint32_t)((r * 100000) / 0x7fff + 10000) + t;
            for (int k = 0; k < 5; k++) GF(0x430678 + k * 4) = 0.0f;
        }
        if (GI(0x4306a8) == 1) {
            GF(0x42d26c) = GF(0x42d26c) - GF(0x424c30);
            GF(0x42d270) = -1.0f;
            GU(0x430698) = (uint32_t)((rand_() * 100000) / 0x7fff + 10000) + t;
            if (GF(0x427e54) < GF(0x42439c)) GF(0x427e54) = -GF(0x427e54);
            GF(0x430678) = 0.0f;
        }
        GI(0x4306a8) = 1 - GI(0x4306a8);
    }
    if (GU(0x430690) < t) {
        GF(0x427e50) = -GF(0x427e50);
        GU(0x430690) = (uint32_t)((rand_() * 300000) / 0x7fff) + t;
    }
    if (GU(0x430698) < t) {
        GF(0x427e54) = -GF(0x427e54);
        GU(0x430698) = (uint32_t)((rand_() * 300000) / 0x7fff) + t;
    }
    GF(0x42eeb8) = DT * GF(0x424c2c);
    const float s = GF(0x42eeb8);
    if (GF(0x424c28) < GF(0x42d268) && GF(0x42439c) < GF(0x427e50)) GF(0x427e50) = -GF(0x427e50);
    if (GF(0x42d268) < GF(0x424c24) && GF(0x427e50) < GF(0x42439c)) GF(0x427e50) = -GF(0x427e50);
    if (GF(0x42d270) < GF(0x424c20) && GF(0x427e54) < GF(0x42439c) && GI(0x4306a8) == 0) {
        if (GF(0x4243a0) <= GF(0x427e18) || GB(0x427e4d) == 0) GF(0x427e54) = -GF(0x427e54);
        else GB(0x430662) = 1;
    }
    GF(0x42d268) = s * GF(0x427e50) + GF(0x42d268);
    int gx = Ftol((GF(0x42d268) + GD(0x424c18)) * GD(0x424c10));
    int gz = Ftol((GF(0x42d270) - GF(0x424b54)) * GD(0x424c08));
    if (gx < 0) gx = 0; else if (gx > 0x13f) gx = 0x13f;
    if (gz < 0) gz = 0; else if (gz > 0x17) gz = 0x17;
    GI(0x42ed38) = gx;
    GI(0x42cec8) = gz;
    GI(0x42f0dc) = gz * 0x140 + gx;
    GI(0x42efc0) = m_floor[GI(0x42f0dc)];
    Matrix* sm = m_starMat;
    const float x = GF(0x42d268);

    if (GI(0x4306a8) == 0) {
        GF(0x42eab4) = GI(0x42efc0) * GF(0x424c04) - GF(0x424c00);
        if (GF(0x42d26c) < GF(0x42eab4)) GF(0x42d26c) = s * GF(0x424af0) + GF(0x42d26c);
        if (GF(0x42eab4) < GF(0x42d26c)) GF(0x42d26c) = GF(0x42d26c) - s * GF(0x424af0);
        float y = GF(0x42d26c);
        float z = s * GF(0x427e54) + GF(0x42d270);
        GF(0x43068c) = GF(0x427e5c) * s + GF(0x43068c);
        GF(0x42d270) = z;
        SetTranslation(sm, x, y, z);
        float a = GF(0x43068c);
        sm->m[0][0] = cosf(a);
        sm->m[0][2] = -sinf(a);
        sm->m[2][0] = sinf(a);
        sm->m[2][2] = cosf(a);
    }
    if (GI(0x4306a8) == 1) {
        GF(0x42eab4) = GI(0x42efc0) * GF(0x424c04) - GF(0x424bfc);
        if (GF(0x42eab4) + GF(0x427e60) + GF(0x424a78) < GF(0x42d26c)) GF(0x427e60) = GF(0x42d26c) - GF(0x42eab4);
        if (GF(0x42d26c) < GF(0x42eab4) + GF(0x427e60)) GF(0x42d26c) = s * GF(0x424bf8) + GF(0x42d26c);
        if (GF(0x42eab4) + GF(0x427e60) < GF(0x42d26c)) GF(0x42d26c) = GF(0x42d26c) - s * GF(0x424bf8);
        if (GU(0x430694) < t) {
            GF(0x427e58) = -GF(0x427e58);
            GU(0x430694) = (uint32_t)((rand_() * 300000) / 0x7fff + 100000) + t;
        }
        if (GF(0x424bf4) < GF(0x427e60) && GF(0x42439c) < GF(0x427e58)) GF(0x427e58) = -GF(0x427e58);
        if (GF(0x427e60) < GF(0x424bf0) && GF(0x427e58) < GF(0x42439c)) {
            if (GF(0x4243a0) <= GF(0x427e18) || GB(0x427e4d) == 0) GF(0x427e58) = -GF(0x427e58);
            else GB(0x430662) = 1;
        }
    }

    Matrix& rotArm = GM(0x42f0e0);
    Matrix& seg = GM(0x42ccc0);
    Matrix& lift = GM(0x42ef80);
    for (int k = 0; k < 5; k++) {
        GI(0x42d274) = k;
        const int k4 = k * 4;
        float v = s * GF(0x427f88 + k4) + GF(0x430664 + k4);
        GF(0x430664 + k4) = v;
        if (GF(0x427f9c + k4) < v && GF(0x42439c) <= GF(0x427f9c + k4)) {
            GF(0x427f9c + k4) = rand_() * GF(0x424bec) * GF(0x424b04);
            GF(0x427f88 + k4) = rand_() * GF(0x424be8) * GF(0x424b04) - GF(0x4243a0);
        }
        if (GF(0x430664 + k4) < GF(0x427f9c + k4) && GF(0x427f9c + k4) < GF(0x42439c)) {
            GF(0x427f9c + k4) = rand_() * GF(0x424398) * GF(0x424b04);
            GF(0x427f88 + k4) = rand_() * GF(0x424a78) * GF(0x424b04) + GF(0x4243a0);
        }
        const float ph = GF(0x430664 + k4), lf = GF(0x430678 + k4);
        D3DVertex* arm = m_arm[k];
        uint8_t* bak = &m_obj[0x414];
        auto segment = [&](uint32_t listAddr, int n) {
            for (int j = 0; j < n; j++) {
                GI(0x4304b8) = j;
                int idx = GI(listAddr + j * 4);
                XformVertex(VP(arm, idx * 0x20), bak + idx * 0x20, seg);
                XformVertex(VP(arm, idx * 0x20), VP(arm, idx * 0x20), rotArm);
            }
        };
        MatRotY(rotArm, k * GF(0x424be4));
        MatRotY(seg, ph * GF(0x424be0));
        MatRotX(lift, lf * GF(0x424bdc));
        seg.m[3][0] = ph * GF(0x424bd8);
        seg.m[3][2] = fabsf(ph) * GF(0x424bd8);
        lift.m[3][2] = lf * GF(0x424bd4);
        MatMul(seg, seg, lift);
        segment(0x427e64, 6);

        MatRotY(seg, ph * GF(0x424bd0));
        MatRotX(lift, lf * GF(0x424bd0));
        seg.m[3][0] = ph * GF(0x424bcc);
        lift.m[3][1] = lf * GF(0x424bc8);
        seg.m[3][2] = fabsf(ph) * GF(0x424bc4);
        lift.m[3][2] = lf * GF(0x424bc0);
        MatMul(seg, seg, lift);
        segment(0x427e7c, 6);

        MatRotY(seg, ph * GF(0x424b24));
        MatRotX(lift, lf * GF(0x424b24));
        seg.m[3][0] = ph * GF(0x424c34);
        lift.m[3][1] = lf * GF(0x424bbc);
        seg.m[3][2] = fabsf(ph) * GF(0x424bb8);
        lift.m[3][2] = lf * GF(0x424bb4);
        MatMul(seg, seg, lift);
        segment(0x427e94, 6);

        MatRotY(seg, ph * GF(0x424bb0));
        MatRotX(lift, lf * GF(0x424bb0));
        seg.m[3][0] = ph * GF(0x424bac);
        lift.m[3][1] = lf * GF(0x424ba8);
        seg.m[3][2] = fabsf(ph) * GF(0x424ba4);
        lift.m[3][2] = lf * GF(0x424ba0);
        MatMul(seg, seg, lift);
        segment(0x427eac, 6);

        MatRotY(seg, ph * GF(0x424b9c));
        MatRotX(lift, lf * GF(0x424b98));
        seg.m[3][0] = ph * GF(0x424b94);
        lift.m[3][1] = lf * GF(0x424b90);
        seg.m[3][2] = fabsf(ph) * GF(0x424b8c);
        lift.m[3][2] = lf * GF(0x424b88);
        MatMul(seg, seg, lift);
        segment(0x427ec4, 6);
        segment(0x427edc, 4);

        if (GI(0x4306a8) == 0) {
            const int tip = GI(0x427ee8);
            D3DVertex* sh = m_starshad;
            sh[k].x = (GF(0x424b84) - lf) * arm[tip].x;
            sh[k].z = (GF(0x424b84) - lf) * arm[tip].z;
            float* w = &GF(0x4301b8 + k * 0x20);
            XformVertex(w, &arm[tip], *sm);
            GF(0x4301b0) = GF(0x424b24) - w[2];
            if (GF(0x4301b0) <= GF(0x42439c)) {
                int tx = Ftol((w[0] + GF(0x424b80)) * GF(0x424b7c));
                int tz = Ftol((w[2] - GF(0x424ae0)) * GF(0x424b78));
                if (tx < 0) tx = 0;
                if (tz < 0) tz = 0;
                if (0x13f < tx) tx = 0x13f;
                if (0x17 < tz) tz = 0x17;
                GI(0x43028c) = tx;
                GI(0x42cecc) = tz;
                GF(0x427f84) = s * GF(0x424b74);
                GI(0x42cd7c) = tz * 0x140 + tx;
                uint32_t h = m_floor[GI(0x42cd7c)];
                if (0xac < h) {
                    GF(0x42d030) = (float)(int)(h - 0xac) * GF(0x424b70);
                    if (GF(0x430678 + k4) < GF(0x42d030)) GF(0x427f84) = s * GF(0x424af0);
                }
                float nl = GF(0x427f84) + GF(0x430678 + k4);
                GI(0x42cd60) = (int)h;
                GF(0x430678 + k4) = nl;
                if (nl < GF(0x42439c)) GF(0x430678 + k4) = 0.0f;
                if (h == 0xff && GF(0x42439c) < GF(0x427e54)) GF(0x427e54) = -GF(0x427e54);
            } else {
                GF(0x430678 + k4) = GF(0x4301b0) * GF(0x424b00);
            }
        }
    }

    if (GI(0x4306a8) == 1) {
        GF(0x42d270) = -1.1f;
        GF(0x427e60) = s * GF(0x427e58) + GF(0x427e60);
        SetTranslation(sm, GF(0x42d268), GF(0x42d26c), -1.1f);
        float c = cosf(GF(0x424b68)), sn = sinf(GF(0x424b68));
        sm->m[1][1] = c;
        sm->m[1][2] = sn;
        sm->m[2][1] = -sn;
        sm->m[2][2] = c;
        if (GF(0x427e60) < GF(0x424b54)) {
            float d = GF(0x424b54) - GF(0x427e60);
            GF(0x430678) = d + d;
            if (GF(0x42439c) < GF(0x427f9c)) { GF(0x427f9c) = 0.6f; GF(0x427f88) = 0.2f; return; }
            GF(0x427f9c) = -0.6f;
            GF(0x427f88) = -0.2f;
        }
    }
}

// ============================================================================
// 0x405e60  foreground / background light levels
void Aquarium::LightLevels()
{
    if (GF(0x427e1c) != 0.0f) {
        GF(0x427e18) = GF(0x427e18) + GF(0x427e1c);
        GF(0x427e1c) = 0.0f;
        if (GF(0x427e18) < 0.0f) GF(0x427e18) = 0.0f;
        if (1.0f < GF(0x427e18)) GF(0x427e18) = 1.0f;
        GF(0x43037c) = GF(0x427e18);
        GF(0x430378) = GF(0x427e18);
        GF(0x430374) = GF(0x427e18);
        ApplyLight();
    }
    if (GF(0x427e20) != 0.0f) {
        GF(0x427e14) = GF(0x427e14) + GF(0x427e20);
        GF(0x427e20) = 0.0f;
        if (0.0f <= GF(0x427e14)) { if (1.0f < GF(0x427e14)) GF(0x427e14) = 1.0f; }
        else GF(0x427e14) = 0.0f;
        int b = Ftol(GF(0x427e14) * GF(0x424b5c));
        int r = b - 0x8b, g = b - 0x4b, r2 = b - 0xd7, g2 = b - 0x91;
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (r2 < 0) r2 = 0;
        if (g2 < 0) g2 = 0;
        uint32_t top = ((uint32_t)(r << 8 | g) << 8) | (uint32_t)b;
        O(TLVertex, 0xc8).color = top;
        O(TLVertex, 0x108).color = top;
        uint32_t b2 = (uint32_t)Ftol(GF(0x427e14) * GF(0x424b58));
        uint32_t bot = ((uint32_t)(r2 << 8 | g2) << 8) | b2;
        O(TLVertex, 0xa8).color = bot;
        O(TLVertex, 0xe8).color = bot;
    }
    if (GF(0x427e14) < 0.2f && GF(0x427e18) < 0.2f) {
        if (GF(0x427e14) <= GF(0x427e18)) GF(0x427e18) = 0.2f;
        else GF(0x427e14) = 0.2f;
    }
    float e = GF(0x43037c) * 0.5f + GF(0x427e14) * 0.3f + 0.1f;
    GF(0x42ec94) = e;
    GF(0x42ec98) = e;
    GF(0x42ec9c) = e;
}

// ============================================================================
// 0x405ab0  bubble columns and single bubbles
void Aquarium::Bubbles()
{
    float d = DT;
    GF(0x42eeb4) = d;
    GF(0x42e98c) = d * 0.0007f;
    GF(0x4304b0) = d * 0.0006f;
    GF(0x42eeb4) = d * 0.004f;
    if (OF(0x7294) < 0.0f) for (int k = 0; k < 4; k++) OF(0x7294 + k * 0x20) += 200.0f;
    if (OF(0x7314) < 0.0f) for (int k = 0; k < 4; k++) OF(0x7314 + k * 0x20) += 200.0f;
    for (int b = 0; b < 12; b++) {
        size_t o = 0x737c + b * 0x80;
        if (GD(0x424b30) < OF(o)) for (int k = 0; k < 4; k++) OF(o + k * 0x20) -= GF(0x424b28);
    }
    static const struct { float hi, lo; } lim[12] = {
        {-1.2f, -2.2f}, {-1.1f, -2.3f}, {-1.1f, -2.3f}, {-1.2f, -2.2f}, {-1.2f, -2.3f}, {-1.1f, -2.2f},
        {-1.2f, -2.2f}, {-1.2f, -2.1f}, {-1.1f, -2.2f}, {-1.2f, -2.3f}, {-1.2f, -2.2f}, {-1.1f, -2.3f},
    };
    for (int b = 0; b < 12; b++) {
        float x = OF(0x7378 + b * 0x80);
        if (lim[b].hi < x || x < lim[b].lo) GF(0x427f54 + b * 4) = -GF(0x427f54 + b * 4);
    }
    for (int v = 0; v < 4; v++) {
        OF(0x7294 + v * 0x20) -= GF(0x42e98c);
        OF(0x7314 + v * 0x20) -= GF(0x4304b0);
        for (int b = 0; b < 12; b++) {
            size_t o = 0x7378 + b * 0x80 + v * 0x20;
            OF(o + 4) = GF(0x42eeb4) + OF(o + 4);
            OF(o) = OF(o) + GF(0x427f54 + b * 4);
        }
    }
}

// 0x405a40  caustic texture scroll
void Aquarium::ScrollUV(D3DVertex* v, uint32_t n)
{
    if (!v || !n) return;
    float wrap = v[0].tu < 0.0f ? 10.0f : 0.0f;
    for (uint32_t k = 0; k < n; k++) v[k].tu = (v[k].tu - DT * 0.0005f) + wrap;
}

// ============================================================================
// 0x40b130  Render
void Aquarium::Render()
{
    Renderer& r = *m_r;
    const float fg = GF(0x427e18);
    const XMaterial defMat = MaterialAt(GADDR(0x430340));
    const int mode = OI(0xa4);                     // 0 = textured (wireframe modes unused)
    Matrix ident;

    r.Clear(0x44 / 255.0f, 0x44 / 255.0f, 0x44 / 255.0f);
    r.SetWorld(ident);
    r.SetView(OM(0x22c));

    // background gradient (pre-transformed quad)
    r.SetZEnable(false);
    r.SetZWrite(false);
    r.SetTexture(0);
    r.SetAlphaBlend(false);
    r.SetAlphaTest(false);
    r.DrawTL(PRIM_TRISTRIP, &O(TLVertex, 0xa8), 4);
    r.SetAlphaBlend(true);

    // far wall and distant caustics
    r.SetClamp(true);
    r.DrawFile(*m_tankdist, 1 < mode, false, defMat, fg);
    r.SetClamp(false);
    r.DrawFile(*m_lightplaydist, 1 < mode, true, defMat, fg);

    // fish behind the rocks
    r.SetZWrite(true);
    r.SetZEnable(true);
    r.SetSpecular(true);
    r.SetAlphaTest(true);
    r.SetClamp(true);
    for (int i = 0; i < GI(0x42ce38); i++)
        if (FISHON(i) != 0 && (-0.2f < PZ(i) || GI(0x4301b4) != 0))
            r.DrawFile(*m_fishFile[i], 0 < mode, true, defMat, fg);
    r.SetClamp(false);
    r.SetAlphaTest(false);
    r.SetSpecular(false);

    // bubble columns
    if (GI(0x427e44) != 0) {
        r.SetMaterial(MaterialAt(GADDR(0x42ec60)));
        r.SetWorld(ident);
        r.SetTexture(r.Texture("bubbles.dds"));
        r.DrawVertices(PRIM_TRISTRIP, &O(D3DVertex, 0x72f8), 4, nullptr, 0);
        r.DrawVertices(PRIM_TRISTRIP, &O(D3DVertex, 0x7278), 4, nullptr, 0);
        for (int b = 0; b < 8; b++) r.DrawVertices(PRIM_TRISTRIP, &O(D3DVertex, 0x7378 + b * 0x80), 4, nullptr, 0);
        r.SetTexture(0);
    }

    // rocks / tank backdrop drawn over the back fish
    r.SetZEnable(false);
    r.SetClamp(true);
    r.DrawFile(*m_bkgrnd, 1 < mode, true, defMat, fg);
    if (m_starfish && GI(0x4306a8) == 0) {
        r.SetZEnable(true);
        r.DrawFile(*m_starfish, 0 < mode, true, defMat, fg);
    }

    // caustics overlay
    r.SetZEnable(false);
    r.SetZWrite(false);
    r.SetClamp(false);
    r.DrawFile(*m_lightplay, 1 < mode, true, defMat, fg);

    // fish in front
    r.SetZEnable(true);
    r.SetZWrite(true);
    r.SetSpecular(true);
    r.SetAlphaTest(true);
    r.SetClamp(true);
    for (int i = 0; i < GI(0x42ce38); i++)
        if (FISHON(i) != 0 && PZ(i) <= -0.2f)
            r.DrawFile(*m_fishFile[i], 0 < mode, true, defMat, fg);
    r.SetClamp(false);
    if (m_starfish && GI(0x4306a8) == 1) {
        r.SetZEnable(false);
        r.DrawFile(*m_starfish, 0 < mode, true, defMat, fg);
    }
    r.SetSpecular(false);
    r.SetAlphaTest(false);
    if (GI(0x4301b4) != 0) GI(0x4301b4) = GI(0x4301b4) - 1;
}

// ============================================================================
// 0x4100a0  frame loop
void Aquarium::Frame(uint32_t now)
{
    if (m_first) { m_lastTime = now; m_first = false; GI(0x4301b4) = 3; }
    GU(0x4306a0) = now;
    uint32_t dt = now - m_lastTime;
    if (GB(0x430660) != 0 && dt < 0xf) { Render(); return; }
    if (0x80 < dt) dt = 0x80;
    GU(0x42eefc) = dt;
    m_time += dt;
    const uint32_t t = m_time;
    GF(0x42ee4c) = DT * GF(0x424d90);

    // automatic day/night light cycle
    int32_t& timer = GI(0x427e24);
    if (timer < 0xea61) { if (timer < 0) timer = 0; }
    else timer = 60000;
    timer -= (int32_t)dt;
    if (GI(0x427e28) != 0 && timer < 0) {
        int32_t& st = GI(0x430654);
        const float step = GF(0x42ee4c);
        float& fg = GF(0x427e18);
        float& bg = GF(0x427e14);
        switch (st) {
        case 6:
            GF(0x427e1c) = step;
            if (fg == GF(0x424b60)) { timer = 60000; st = 0; }
            break;
        case 5:
            GF(0x427e1c) = -step;
            GF(0x427e20) = step;
            if (fg == GF(0x424e08)) { timer = 30000; st = 6; }
            break;
        case 4:
            GF(0x427e20) = -step;
            if (bg == GF(0x42439c) || fg < GF(0x424b60)) { timer = 30000; st = 5; }
            break;
        case 3:
            GF(0x427e1c) = step;
            if (fg == GF(0x424394)) { timer = 60000; st = 4; }
            break;
        case 2:
            GF(0x427e1c) = -step;
            if (fg == GF(0x42439c) || bg < GF(0x424394)) { timer = 30000; st = 3; }
            break;
        case 1:
            GF(0x427e20) = step;
            if (bg == GF(0x424394)) { timer = 30000; st = 2; }
            break;
        case 0:
            GF(0x427e20) = -step;
            if (bg == GF(0x42439c) || fg < GF(0x424394)) { timer = 30000; st = 1; }
            break;
        default:
            break;
        }
    }
    m_lastTime = now;

    for (int i = 0; i < GI(0x42ce38); i++)
        if (FISHON(i) != 0) FishMove(t, i);
    StarfishMove(t);
    LightLevels();
    if (GI(0x427e44) != 0) Bubbles();
    ScrollUV(m_lpVerts, m_lpCount);
    ScrollUV(m_lpdVerts, m_lpdCount);
    Render();
}

void Aquarium::AdjustForeground(float d)
{
    GI(0x427e28) = 0;           // manual change stops the automatic cycle
    GF(0x427e1c) = d;
}

void Aquarium::AdjustBackground(float d)
{
    GI(0x427e28) = 0;
    GF(0x427e20) = d;
}

void Aquarium::ToggleBubbles() { GI(0x427e44) = GI(0x427e44) ? 0 : 1; }
bool Aquarium::BubblesOn() { return GI(0x427e44) != 0; }
float Aquarium::Foreground() { return GF(0x427e18); }
float Aquarium::Background() { return GF(0x427e14); }
float Aquarium::StarfishX() { return GF(0x42d268); }
bool Aquarium::StarfishOnGlass() { return GI(0x4306a8) == 1; }
