// InstallShield 5 cabinet reader (data1.hdr + data1.cab), single volume.
#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Extracts the file whose name matches (case-insensitive) from an IS5 cabinet.
// hdrPath: data1.hdr, cabPath: data1.cab. Returns false and sets err on failure.
bool CabExtract(const std::string& hdrPath, const std::string& cabPath,
                const std::string& fileName, std::vector<uint8_t>& out, std::string& err);
