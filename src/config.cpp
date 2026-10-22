// aquarium.ini settings (replaces the original registry values).
#include "config.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static bool ToBool(const std::string& v)
{
    return v == "1" || strcasecmp(v.c_str(), "true") == 0 || strcasecmp(v.c_str(), "on") == 0 ||
           strcasecmp(v.c_str(), "yes") == 0;
}

bool LoadConfig(const std::string& path, Config& cfg)
{
    if (cfg.fish.empty()) cfg.fish.assign(8, "random");
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';' || s[0] == '[') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(s.substr(0, eq)), v = Trim(s.substr(eq + 1));
        if (strncasecmp(k.c_str(), "Fish", 4) == 0 && k.size() == 5 && k[4] >= '1' && k[4] <= '8')
            cfg.fish[k[4] - '1'] = v;
        else if (strcasecmp(k.c_str(), "FishCount") == 0) cfg.fishCount = atoi(v.c_str());
        else if (strcasecmp(k.c_str(), "Sound") == 0) cfg.sound = ToBool(v);
        else if (strcasecmp(k.c_str(), "BubbleColumn") == 0) cfg.bubbles = ToBool(v);
        else if (strcasecmp(k.c_str(), "AutoLights") == 0) cfg.autoLights = ToBool(v);
        else if (strcasecmp(k.c_str(), "BackgroundLights") == 0) cfg.backgroundLights = atoi(v.c_str());
        else if (strcasecmp(k.c_str(), "ForegroundLights") == 0) cfg.foregroundLights = atoi(v.c_str());
        else if (strcasecmp(k.c_str(), "StarfishOnGlass") == 0) cfg.starfishOnGlass = ToBool(v);
        else if (strcasecmp(k.c_str(), "StarfishAllowedGlass") == 0) cfg.starfishAllowedGlass = ToBool(v);
        else if (strcasecmp(k.c_str(), "NoDuplicateRandom") == 0) cfg.noDuplicateRandom = ToBool(v);
        else if (strcasecmp(k.c_str(), "CalcNormals") == 0) cfg.calcNormals = ToBool(v);
        else if (strcasecmp(k.c_str(), "Limit60") == 0) cfg.limit60 = ToBool(v);
        else if (strcasecmp(k.c_str(), "StarFishPos") == 0) cfg.starfishPos = (float)atof(v.c_str());
    }
    fclose(f);
    if (cfg.backgroundLights < 0) cfg.backgroundLights = 0;
    if (cfg.backgroundLights > 100) cfg.backgroundLights = 100;
    if (cfg.foregroundLights < 0) cfg.foregroundLights = 0;
    if (cfg.foregroundLights > 100) cfg.foregroundLights = 100;
    if (cfg.fishCount < 1) cfg.fishCount = 1;
    if (cfg.fishCount > 8) cfg.fishCount = 8;
    return true;
}

bool SaveConfig(const std::string& path, const Config& cfg)
{
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return false;
    fprintf(f, "# Marine Aquarium 2 settings\n");
    fprintf(f, "# FishN: random, off, or a fish name (e.g. Yellow_Tang, LionFish, Percula_Clownfish)\n");
    fprintf(f, "FishCount=%d\n", cfg.fishCount);
    for (int k = 0; k < 8; k++)
        fprintf(f, "Fish%d=%s\n", k + 1, k < (int)cfg.fish.size() ? cfg.fish[k].c_str() : "random");
    fprintf(f, "Sound=%d\n", cfg.sound ? 1 : 0);
    fprintf(f, "BubbleColumn=%d\n", cfg.bubbles ? 1 : 0);
    fprintf(f, "AutoLights=%d\n", cfg.autoLights ? 1 : 0);
    fprintf(f, "BackgroundLights=%d\n", cfg.backgroundLights);
    fprintf(f, "ForegroundLights=%d\n", cfg.foregroundLights);
    fprintf(f, "StarfishOnGlass=%d\n", cfg.starfishOnGlass ? 1 : 0);
    fprintf(f, "StarfishAllowedGlass=%d\n", cfg.starfishAllowedGlass ? 1 : 0);
    fprintf(f, "NoDuplicateRandom=%d\n", cfg.noDuplicateRandom ? 1 : 0);
    fprintf(f, "CalcNormals=%d\n", cfg.calcNormals ? 1 : 0);
    fprintf(f, "Limit60=%d\n", cfg.limit60 ? 1 : 0);
    fprintf(f, "StarFishPos=%.2f\n", cfg.starfishPos);
    fclose(f);
    return true;
}
