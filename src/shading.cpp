#include "solarc/shading.h"
#include "solarc/texture.h"

#include <algorithm>

namespace solarc {

ShadedColor shade(const Material& mat, const ShadingInput& input,
                  const DirectionalLight& light) noexcept {
    // 1. Albedo: texture texel multiplied by the base color, or base color alone.
    Rgba albedo = mat.baseColor;
    if (mat.albedo && !mat.albedo->empty()) {
        const Rgba texel = sampleTexture(*mat.albedo, input.uv.x, input.uv.y);
        albedo.r *= texel.r;
        albedo.g *= texel.g;
        albedo.b *= texel.b;
        albedo.a *= texel.a;
    }

    // 2. Unlit / sun-emissive paths. Both bypass the Lambert calculation.
    if (mat.unlit) {
        return {albedo};
    }
    if (mat.sunEmissive) {
        return {{
            albedo.r * mat.sunEmissiveColor.r,
            albedo.g * mat.sunEmissiveColor.g,
            albedo.b * mat.sunEmissiveColor.b,
            albedo.a
        }};
    }

    // 3. Lambert diffuse + constant ambient. For two-sided rings the mesh
    //    already stores an explicit underside with its own -Y normal; module B
    //    should use Material::doubleSided to decide whether to disable back-face
    //    culling, and pass the interpolated (already oriented) normal here.
    Vec3 n = input.normal.normalized();
    const float nDotL = std::max(0.0f, n.dot(light.direction));
    const float diff = mat.diffuse * nDotL;

    const Rgba out{
        albedo.r * (mat.ambient + diff * light.intensity.r),
        albedo.g * (mat.ambient + diff * light.intensity.g),
        albedo.b * (mat.ambient + diff * light.intensity.b),
        albedo.a
    };
    return {out};
}

Material makeSunMaterial() {
    Material m;
    m.name = "sun-emissive";
    m.sunEmissive = true;
    m.sunEmissiveColor = {1.0f, 0.93f, 0.78f, 1.0f};
    m.baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    return m;
}

Material makePlanetMaterial(TexturePtr albedo, float ambient) {
    Material m;
    m.name = "planet-lambert";
    m.albedo = std::move(albedo);
    m.ambient = ambient;
    m.diffuse = 1.0f;
    return m;
}

Material makeRingMaterial(TexturePtr bands, bool doubleSided) {
    Material m;
    m.name = "ring-lambert";
    m.albedo = std::move(bands);
    m.ambient = 0.12f;
    m.diffuse = 0.9f;
    m.doubleSided = doubleSided;
    return m;
}

Material makeOrbitMaterial(Rgba color) {
    Material m;
    m.name = "orbit-unlit";
    m.baseColor = color;
    m.unlit = true;
    return m;
}

} // namespace solarc
