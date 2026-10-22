// InstallShield 5 cabinet reader (data1.hdr + data1.cab), single volume.
// Format per unshield (twogood/unshield, MIT): header file holds the cabinet
// descriptor and file table; data file holds raw-deflate chunks, each prefixed
// by a 16-bit compressed length.
#include "cab.h"
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <zlib.h>

static bool ReadAll(const std::string& path, std::vector<uint8_t>& buf)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf.resize(n > 0 ? n : 0);
    bool ok = n > 0 && fread(buf.data(), 1, n, f) == (size_t)n;
    fclose(f);
    return ok;
}

static uint32_t U32(const std::vector<uint8_t>& b, size_t o)
{
    if (o + 4 > b.size()) return 0;
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

static uint16_t U16(const std::vector<uint8_t>& b, size_t o)
{
    if (o + 2 > b.size()) return 0;
    return b[o] | (b[o + 1] << 8);
}

static void Deobfuscate(uint8_t* p, size_t n, unsigned& seed)
{
    for (size_t i = 0; i < n; i++, seed++) {
        uint8_t v = p[i] ^ 0xd5;
        v = (uint8_t)((v >> 2) | (v << 6));
        p[i] = (uint8_t)(v - (seed % 0x47));
    }
}

bool CabExtract(const std::string& hdrPath, const std::string& cabPath,
                const std::string& fileName, std::vector<uint8_t>& out, std::string& err)
{
    std::vector<uint8_t> hdr, cab;
    if (!ReadAll(hdrPath, hdr)) { err = "cannot read " + hdrPath; return false; }
    if (!ReadAll(cabPath, cab)) { err = "cannot read " + cabPath; return false; }
    if (U32(hdr, 0) != 0x28635349) { err = "bad header signature"; return false; }

    const uint32_t descOff   = U32(hdr, 0x0c);
    const uint32_t tableOff  = U32(hdr, descOff + 0x0c);
    const uint32_t dirCount  = U32(hdr, descOff + 0x1c);
    const uint32_t fileCount = U32(hdr, descOff + 0x28);
    const size_t   tableBase = (size_t)descOff + tableOff;

    for (uint32_t i = 0; i < fileCount; i++) {
        size_t fd = tableBase + U32(hdr, tableBase + (dirCount + i) * 4);
        if (fd + 0x3a > hdr.size()) break;
        size_t nameOff = tableBase + U32(hdr, fd);
        if (nameOff >= hdr.size()) continue;
        const char* name = (const char*)&hdr[nameOff];
        if (strcasecmp(name, fileName.c_str()) != 0) continue;

        const uint16_t flags    = U16(hdr, fd + 8);
        const uint32_t expanded = U32(hdr, fd + 10);
        const uint32_t packed   = U32(hdr, fd + 14);
        const uint32_t dataOff  = U32(hdr, fd + 0x26);
        if ((size_t)dataOff + packed > cab.size()) { err = "data out of range"; return false; }

        std::vector<uint8_t> raw(cab.begin() + dataOff, cab.begin() + dataOff + packed);
        if (flags & 2) { unsigned seed = 0; Deobfuscate(raw.data(), raw.size(), seed); }

        out.clear();
        out.reserve(expanded);
        if (!(flags & 4)) { out = raw; return true; }

        std::vector<uint8_t> chunk(0x10000 + 1), dst(0x10000);
        size_t p = 0;
        while (p + 2 <= raw.size() && out.size() < expanded) {
            uint16_t len = raw[p] | (raw[p + 1] << 8);
            p += 2;
            if (len == 0 || p + len > raw.size()) { err = "bad chunk"; return false; }
            memcpy(chunk.data(), &raw[p], len);
            chunk[len] = 0;
            p += len;

            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            zs.next_in = chunk.data();
            zs.avail_in = len + 1;
            zs.next_out = dst.data();
            zs.avail_out = dst.size();
            if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) { err = "inflateInit failed"; return false; }
            int r = inflate(&zs, Z_FINISH);
            size_t got = zs.total_out;
            inflateEnd(&zs);
            if (r != Z_STREAM_END) { err = "inflate failed"; return false; }
            out.insert(out.end(), dst.begin(), dst.begin() + got);
        }
        if (out.size() != expanded) { err = "size mismatch"; return false; }
        return true;
    }
    err = fileName + " not found in cabinet";
    return false;
}
