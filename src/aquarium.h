// Marine Aquarium 2 scene logic, ported from the decompiled Windows .scr.
//
// The original keeps its state in one large C++ object plus file-scope
// globals. To stay faithful, both are emulated as byte blobs addressed by the
// original offsets: m_obj[] is the application object (this + offset) and
// g_mem[] mirrors the .scr image from VA 0x424000 (.rdata, .data, .bss), so
// every constant and initial value comes straight from the user's own .scr.
// Pointer-valued fields live in typed side tables (64-bit pointers do not
// fit the original 32-bit slots).
#pragma once
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include "xfile.h"
#include "render.h"
#include "config.h"

class PeResources;

class Aquarium {
public:
    Aquarium();
    bool Init(const std::vector<uint8_t>& scrImage, const PeResources* res, Renderer* r,
              const Config& cfg, std::string& err);
    // One frame: advance by the elapsed real time, then draw.
    void Frame(uint32_t nowMs);
    // Light level controls (D-pad), +/- step like the original key handler.
    void AdjustForeground(float delta);
    void AdjustBackground(float delta);
    void ToggleBubbles();
    bool BubblesOn();
    float Foreground();
    float Background();
    float StarfishX();
    bool StarfishOnGlass();

private:
    // ---- ported routines (original addresses) --------------------------
    void Construct();                         // 0x4040c0 (scene part)
    bool LoadFish(std::string& err);          // 0x4042e0
    bool OneTimeSceneInit(std::string& err);  // 0x404db0
    void InitDeviceObjects();                 // 0x40b8d0
    void FishMove(uint32_t timeMs, int i);    // 0x4072a0
    void StarfishMove(uint32_t timeMs);       // 0x4060b0
    void LightLevels();                       // 0x405e60
    void ApplyLight();                        // 0x402e10
    void Bubbles();                           // 0x405ab0
    void ScrollUV(D3DVertex* v, uint32_t n);  // 0x405a40
    void BendVertices(D3DVertex* out, const D3DVertex* bend, const D3DVertex* base,
                      uint32_t n, float x, float y);  // 0x4059b0
    void Render();                            // 0x40b130
    void SetDrawOrder(int i, int sector);     // tail of 0x4072a0

    // ---- emulated memory ------------------------------------------------
    std::vector<uint8_t> g_mem;               // VA 0x424000 .. 0x432000
    std::vector<uint8_t> m_obj;               // application object, 0x7a00 bytes

    // pointer side tables
    std::unique_ptr<XFile> m_fishFile[12];
    std::unique_ptr<XFile> m_bkgrnd, m_tankdist, m_lightplay, m_lightplaydist, m_starfish;
    XObject* m_frame[12][7] = {};             // 0x2a8 + i*0x1c: whole, body, eyes, lfin, rfin, llow, rlow
    D3DVertex* m_v958[12] = {};               // fishbody (live)
    std::vector<D3DVertex> m_v988[12];        // fishbody copy (rest pose)
    D3DVertex* m_v9b8[12] = {};               // fishbend
    D3DVertex* m_v9e8[12] = {};               // eyes
    D3DVertex* m_va18[12] = {};               // leftfin
    D3DVertex* m_va48[12] = {};               // rightfin
    D3DVertex* m_va78[12] = {};               // leftlowfin
    D3DVertex* m_vaa8[12] = {};               // rightlowfin
    Matrix* m_m7108[12] = {};                 // Wholefish matrix
    Matrix* m_m7138[12] = {};                 // Frame_leftfin matrix
    Matrix* m_m7168[12] = {};                 // Frame_rightfin matrix
    Matrix* m_m7198[12] = {};                 // Frame_eyes matrix
    D3DVertex* m_arm[5] = {};                 // 0x400..0x410 starfish arms
    D3DVertex* m_starshad = nullptr;          // 0x954
    Matrix* m_starMat = nullptr;              // 0x42ed34
    const uint8_t* m_floor = nullptr;         // 0x42ef44 FLOORDATA
    D3DVertex* m_lpVerts = nullptr;           // 0x430504
    D3DVertex* m_lpdVerts = nullptr;          // 0x430508
    uint32_t m_lpCount = 0, m_lpdCount = 0;   // 0x42d264, 0x42d034

    std::vector<std::string> m_slots;         // configured fish names (0x42fe4c)
    std::vector<std::string> m_chosen;        // resolved names (0x42d2b8)

    const PeResources* m_res = nullptr;
    Renderer* m_r = nullptr;
    Config m_cfg;
    uint32_t m_lastTime = 0;
    uint32_t m_time = 0;                      // 0x43069c accumulated time
    bool m_first = true;
};
