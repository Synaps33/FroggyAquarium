// DDS (DXT1 / DXT3 / DXT5) decoder to RGBA8888, top mip level only.
#pragma once
#include <vector>
#include <string>
#include <cstdint>

bool DdsDecode(const std::vector<uint8_t>& dds, int& w, int& h, std::vector<uint8_t>& rgba, std::string& err);
