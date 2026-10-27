// Deterministic host test runner: renders the aquarium into the software
// framebuffer and dumps screenshots, without SDL. Used for A/B comparison with
// the GLES reference build and for performance measurements.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <ctime>

#include "cab.h"
#include "pe.h"
#include "render.h"
#include "aquarium.h"
#include "config.h"

static void SavePpm(const std::string& path, const uint16_t* fb, int w, int h)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); return; }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<uint8_t> row((size_t)w * 3);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t p = fb[(size_t)y * w + x];
            int r5 = (p >> 11) & 31, g6 = (p >> 5) & 63, b5 = p & 31;
            row[x * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            row[x * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
            row[x * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
}

static double NowMs()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main(int argc, char** argv)
{
    std::string dataDir = "assets";
    std::string outPrefix = "screenshots/frame";
    std::string cfgPath;
    int benchFrames = 0;
    std::vector<int> shots;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dataDir = argv[++i];
        else if (!strcmp(argv[i], "--config") && i + 1 < argc) cfgPath = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPrefix = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            char* s = argv[++i];
            while (*s) { shots.push_back(atoi(s)); while (*s && *s != ',') s++; if (*s == ',') s++; }
        } else if (!strcmp(argv[i], "--bench") && i + 1 < argc) {
            benchFrames = atoi(argv[++i]);
        }
    }
    if (cfgPath.empty()) cfgPath = dataDir + "/aquarium.ini";

    Config cfg;
    LoadConfig(cfgPath, cfg);

    std::vector<uint8_t> scr;
    std::string err;
    if (!CabExtract(dataDir + "/data1.hdr", dataDir + "/data1.cab", "Marine Aquarium 2.scr", scr, err)) {
        fprintf(stderr, "aquarium: %s\n", err.c_str());
        return 1;
    }
    std::vector<uint8_t> image = scr;
    PeResources res;
    if (!res.Load(std::move(scr), err)) { fprintf(stderr, "aquarium: %s\n", err.c_str()); return 1; }

    int rw = 320, rh = 240;
    if (const char* ws = getenv("AQUARIUM_W")) rw = atoi(ws);
    if (const char* hs = getenv("AQUARIUM_H")) rh = atoi(hs);
    Renderer r;
    if (!r.Init(rw, rh, &res, err)) { fprintf(stderr, "aquarium: %s\n", err.c_str()); return 1; }
    Aquarium aq;
    if (!aq.Init(image, &res, &r, cfg, err)) { fprintf(stderr, "aquarium: %s\n", err.c_str()); return 1; }
    image.clear();
    image.shrink_to_fit();

    int maxFrame = benchFrames;
    for (int s : shots) if (s > maxFrame) maxFrame = s;
    if (maxFrame < 1) maxFrame = 1;

    uint32_t simTime = 1000;
    int frame = 0;
    size_t shotIdx = 0;
    double t0 = NowMs();
    for (int f = 1; f <= maxFrame; f++) {
        simTime += 16;
        aq.Frame(simTime);
        frame = f;
        while (shotIdx < shots.size() && shots[shotIdx] == frame) {
            char name[512];
            snprintf(name, sizeof(name), "%s_%03d.ppm", outPrefix.c_str(), frame);
            SavePpm(name, r.Framebuffer(), r.Width(), r.Height());
            printf("[RUNNER] frame %d -> %s\n", frame, name);
            shotIdx++;
        }
    }
    double t1 = NowMs();
    if (benchFrames > 0)
        printf("[RUNNER] %d frames in %.1f ms (%.2f ms/frame)\n", benchFrames, t1 - t0, (t1 - t0) / benchFrames);
    return 0;
}
