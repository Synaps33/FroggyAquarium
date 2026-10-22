// Minimal PE32 resource reader for the Marine Aquarium 2 .scr.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

class PeResources {
public:
    bool Load(std::vector<uint8_t>&& image, std::string& err);
    // type: numeric id as decimal string ("10" = RCDATA) or a name ("WAV").
    // Lookup is case-insensitive. Returns nullptr if absent.
    const std::vector<uint8_t>* Find(const std::string& type, const std::string& name) const;

private:
    struct Entry { std::string type, name; std::vector<uint8_t> data; };
    std::vector<Entry> m_entries;
};
