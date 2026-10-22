// Minimal PE32 resource reader for the Marine Aquarium 2 .scr.
#include "pe.h"
#include <strings.h>

static uint32_t R32(const std::vector<uint8_t>& b, size_t o)
{
    if (o + 4 > b.size()) return 0;
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

static uint16_t R16(const std::vector<uint8_t>& b, size_t o)
{
    if (o + 2 > b.size()) return 0;
    return b[o] | (b[o + 1] << 8);
}

bool PeResources::Load(std::vector<uint8_t>&& img, std::string& err)
{
    m_entries.clear();
    if (img.size() < 0x40 || img[0] != 'M' || img[1] != 'Z') { err = "not a PE file"; return false; }
    const uint32_t pe = R32(img, 0x3c);
    if (R32(img, pe) != 0x00004550) { err = "bad PE signature"; return false; }
    const uint16_t nSect   = R16(img, pe + 6);
    const uint16_t optSize = R16(img, pe + 20);
    const size_t   opt     = pe + 24;
    const uint32_t rsrcRva = R32(img, opt + 96 + 2 * 8);   // data directory 2 = resources
    const size_t   sect    = opt + optSize;

    // map RVA -> file offset via section table
    auto rvaToOff = [&](uint32_t rva) -> size_t {
        for (int i = 0; i < nSect; i++) {
            size_t s = sect + i * 40;
            uint32_t va = R32(img, s + 12), vsz = R32(img, s + 8), raw = R32(img, s + 20), rsz = R32(img, s + 16);
            uint32_t sz = vsz > rsz ? vsz : rsz;
            if (rva >= va && rva < va + sz) return raw + (rva - va);
        }
        return (size_t)-1;
    };
    const size_t base = rvaToOff(rsrcRva);
    if (base == (size_t)-1) { err = "no resource section"; return false; }

    auto nameOf = [&](uint32_t id) -> std::string {
        if (!(id & 0x80000000u)) return std::to_string(id);
        size_t o = base + (id & 0x7fffffffu);
        uint16_t len = R16(img, o);
        std::string s;
        for (uint16_t i = 0; i < len; i++) s += (char)R16(img, o + 2 + i * 2);
        return s;
    };
    auto entries = [&](size_t dir, auto&& fn) {
        uint16_t n = R16(img, dir + 12) + R16(img, dir + 14);
        for (uint16_t i = 0; i < n; i++) fn(R32(img, dir + 16 + i * 8), R32(img, dir + 20 + i * 8));
    };

    entries(base, [&](uint32_t typeId, uint32_t typeOff) {
        if (!(typeOff & 0x80000000u)) return;
        std::string type = nameOf(typeId);
        entries(base + (typeOff & 0x7fffffffu), [&](uint32_t nameId, uint32_t nameOff) {
            if (!(nameOff & 0x80000000u)) return;
            std::string name = nameOf(nameId);
            entries(base + (nameOff & 0x7fffffffu), [&](uint32_t, uint32_t langOff) {
                if (langOff & 0x80000000u) return;
                size_t de = base + langOff;
                size_t off = rvaToOff(R32(img, de));
                uint32_t size = R32(img, de + 4);
                if (off == (size_t)-1 || off + size > img.size()) return;
                m_entries.push_back({type, name, std::vector<uint8_t>(img.begin() + off, img.begin() + off + size)});
            });
        });
    });
    if (m_entries.empty()) { err = "no resources found"; return false; }
    return true;
}

const std::vector<uint8_t>* PeResources::Find(const std::string& type, const std::string& name) const
{
    for (const auto& e : m_entries)
        if (strcasecmp(e.type.c_str(), type.c_str()) == 0 && strcasecmp(e.name.c_str(), name.c_str()) == 0)
            return &e.data;
    return nullptr;
}
