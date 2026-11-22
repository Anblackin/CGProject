// Module C - texture loading and sampling.
// Textures on disk are 8-bit sRGB PPM (P6) files; they are decoded to linear
// float on load. Sampling and all shading happen in linear space.
#pragma once

#include "types.h"
#include <string>

namespace solarc {

float srgbChannelToLinear(float c) noexcept;
float linearChannelToSrgb(float c) noexcept;
Rgba srgbToLinear(const Rgba& c) noexcept;
Rgba linearToSrgb(const Rgba& c) noexcept;

// Loads a binary PPM (magic "P6", maxval 255). Returns false on any malformed
// file. Pixel row 0 of the file becomes row 0 of the texture (v = 0).
bool loadPPM(const std::string& path, Texture& out);

// Writes a linear-float texture back to an 8-bit sRGB PPM file.
bool savePPM(const std::string& path, const Texture& tex);

// Sample with the texture's own wrap/filter settings.
Rgba sampleTexture(const Texture& tex, float u, float v) noexcept;

// Procedural checker used by the raster tests: 8x8 alternating sRGB tiles.
Texture makeCheckerTexture(int size = 8);

// Procedural Saturn ring band texture (256 x 1, sRGB grayscale with gaps).
Texture makeRingBandTexture();

// Procedural low-resolution planet test texture (warm bands + polar caps).
Texture makePlanetTestTexture(int width = 256, int height = 128);

} // namespace solarc
