// Module C - shared data structures. These are the "Vertex / Mesh / Material"
// structures promised in PROJECT_PLAN.md section 2 (public interfaces).
#pragma once

#include "math.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace solarc {

// One rasterizer vertex. Position/normal are object space; the model matrix
// supplied by module A moves them into world space.
struct Vertex {
    Vec3 position{};
    Vec3 normal{};
    Vec2 uv{};
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::string name;
};

// ---------------------------------------------------------------------------
// Texture image. Stored in LINEAR RGB, float, row-major: index = y * w + x.
// Row 0 is the TOP of the image (v = 0 in our UV convention).
// ---------------------------------------------------------------------------
enum class WrapMode { Repeat, Clamp, Mirror };
enum class FilterMode { Nearest, Bilinear };

struct Rgba {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

struct Texture {
    int width = 0;
    int height = 0;
    std::vector<Rgba> pixels;
    WrapMode wrapU = WrapMode::Repeat;
    WrapMode wrapV = WrapMode::Clamp;
    FilterMode filter = FilterMode::Bilinear;

    bool empty() const noexcept { return width <= 0 || height <= 0 || pixels.empty(); }
};

using TexturePtr = std::shared_ptr<const Texture>;

// ---------------------------------------------------------------------------
// Material. Shading is evaluated in LINEAR space; display conversion is a
// separate final step owned by module D/E (Resolve / present).
//
// sunEmissive: if true, final = sunEmissiveColor * albedo texel (no lighting).
// diffuse:     Lambert term from lightDir (direction TO the light).
// ambient:     constant fill, independent of normal.
// unlit:       bypass lighting entirely (used for orbit ribbons).
// ---------------------------------------------------------------------------
struct Material {
    std::string name;
    TexturePtr albedo;              // nullable: use baseColor when absent
    Rgba baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    bool sunEmissive = false;
    Rgba sunEmissiveColor{1.0f, 0.93f, 0.78f, 1.0f};
    float ambient = 0.06f;
    float diffuse = 1.0f;
    bool unlit = false;
    bool doubleSided = false;
};

// Light direction points FROM the shaded point TOWARD the light source.
// Intensity is a linear multiplier on the albedo.
struct DirectionalLight {
    Vec3 direction{0.0f, 1.0f, 0.0f};
    Rgba intensity{1.0f, 1.0f, 1.0f, 1.0f};
};

// Input assembled by the rasterizer after perspective-correct interpolation.
struct ShadingInput {
    Vec3 worldPos{};
    Vec3 normal{};       // interpolated, then normalized by the shader
    Vec2 uv{};
};

// Result consumed by SampleBuffer / Resolve. Always linear RGBA.
struct ShadedColor {
    Rgba linear{};
};

} // namespace solarc
