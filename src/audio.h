// MS-ADPCM (Microsoft ADPCM) WAV decoder and looping mixer for the aquarium
// ambient sound. Decodes to 16-bit stereo at the source rate and resamples to
// 44100 Hz by sample doubling (the asset is 22050 Hz).
#pragma once
#include <cstdint>
#include <vector>

// Decodes the first audio stream of an in-memory WAV. Returns false on any
// unsupported/invalid input. Output is interleaved stereo PCM16 at 44100 Hz.
bool AudioLoad(const std::vector<uint8_t>& wav, int volume255);
void AudioStop();
// Fills `frames` stereo frames; call between AudioLoad/AudioStop.
void AudioRender(int16_t* out, int frames);
