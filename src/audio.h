// Looped ambient sound (BUBBLES6H.WAV, MS-ADPCM) through SDL audio.
#pragma once
#include <vector>
#include <cstdint>

bool AudioStart(const std::vector<uint8_t>& wav, float volume);
void AudioStop();
