// aquarium.ini settings (replaces the original HKCU\Software\SereneScreen\MarineAquarium2 values).
#pragma once
#include <string>
#include <vector>

struct Config {
    std::vector<std::string> fish;     // Fish1..Fish8: name, "random" or "off"
    int  fishCount = 7;                // number of fish slots used (1..8)
    bool sound = true;                 // Sound
    bool bubbles = true;               // BubbleColumn
    bool autoLights = true;            // AutoLights (day/night cycle)
    int  backgroundLights = 100;       // BackgroundLights 0..100
    int  foregroundLights = 100;       // ForegroundLights 0..100
    bool starfishOnGlass = false;      // StarfishOnGlass (start position)
    bool starfishAllowedGlass = true;  // StarfishAllowedGlass
    bool noDuplicateRandom = false;    // NoDuplicateRandom
    bool calcNormals = false;          // CalcNormals
    bool limit60 = false;              // Limit60
    float starfishPos = -3.5f;         // StarFishPos (x)
};

// Loads key=value pairs; missing keys keep defaults. Returns false if the file
// is absent (defaults are still valid).
bool LoadConfig(const std::string& path, Config& cfg);
// Writes the current settings (used to persist D-pad light changes).
bool SaveConfig(const std::string& path, const Config& cfg);
