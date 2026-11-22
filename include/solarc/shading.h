// Module C - material / lighting evaluation.
// Contract with module B: shade() is called once per shading point (once per
// pixel for SSAA, once per covered triangle for MSAA). It never samples depth
// and never writes to the framebuffer; it is a pure function of its inputs.
#pragma once

#include "types.h"

namespace solarc {

// Evaluate the material at one interpolation point. Returns LINEAR RGBA.
// - worldPos is currently unused but kept in ShadingInput for future falloff.
// - normal must already be in world space; it is normalized inside.
// - lightDir points toward the light source; normalize it before calling.
ShadedColor shade(const Material& mat, const ShadingInput& input,
                  const DirectionalLight& light) noexcept;

// Convenience material factories used by the demo and by module A's scene.
Material makeSunMaterial();
Material makePlanetMaterial(TexturePtr albedo, float ambient = 0.06f);
Material makeRingMaterial(TexturePtr bands, bool doubleSided = true);
Material makeOrbitMaterial(Rgba color);

} // namespace solarc
