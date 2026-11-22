// Module C visual verification: a tiny CPU rasterizer that renders the meshes
// produced by this module with its own texture sampler and shader.
// This is NOT the production renderer (module B owns that); it exists so module
// C can be validated independently and so the 11/18 milestone has an image.
//
// Build: demo_render
// Run:   demo_render [output.ppm] [width] [height]
#include "solarc/geometry.h"
#include "solarc/shading.h"
#include "solarc/texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct RasterVertex {
    solarc::Vec4 clip{};
    solarc::Vec3 world{};
    solarc::Vec3 normal{};
    solarc::Vec2 uv{};
};

struct Framebuffer {
    int width = 0, height = 0;
    std::vector<solarc::Rgba> color;
    std::vector<float> depth;

    Framebuffer(int w, int h) : width(w), height(h),
        color(static_cast<std::size_t>(w) * h),
        depth(static_cast<std::size_t>(w) * h, 1e9f) {}
};

void drawMesh(Framebuffer& fb, const solarc::Mesh& mesh, const solarc::Mat4& model,
              const solarc::Mat4& viewProj, const solarc::Material& material,
              const solarc::Vec3& lightPos) {
    // Transform every vertex once.
    std::vector<RasterVertex> vs;
    vs.reserve(mesh.vertices.size());
    for (const solarc::Vertex& v : mesh.vertices) {
        const solarc::Vec4 world4 = model * solarc::Vec4(v.position, 1.0f);
        RasterVertex out;
        out.world = {world4.x, world4.y, world4.z};
        out.normal = model.transformDirection(v.normal).normalized();
        out.uv = v.uv;
        out.clip = viewProj * world4;
        vs.push_back(out);
    }

    const float halfW = static_cast<float>(fb.width) * 0.5f;
    const float halfH = static_cast<float>(fb.height) * 0.5f;

    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        RasterVertex v[3] = {
            vs[mesh.indices[t]], vs[mesh.indices[t + 1]], vs[mesh.indices[t + 2]]
        };

        // Skip triangles crossing the near plane (the demo scene stays in front).
        if (v[0].clip.w <= 0.01f || v[1].clip.w <= 0.01f || v[2].clip.w <= 0.01f) continue;

        solarc::Vec2 p[3];
        float invW[3];
        for (int i = 0; i < 3; ++i) {
            const float x = v[i].clip.x / v[i].clip.w;
            const float y = v[i].clip.y / v[i].clip.w;
            const float z = v[i].clip.z / v[i].clip.w;
            p[i] = {x * halfW + halfW, (1.0f - y) * halfH};
            invW[i] = 1.0f / v[i].clip.w;
            v[i].clip = {x, y, z, v[i].clip.w};
        }

        const float minX = std::max(0.0f, std::min({p[0].x, p[1].x, p[2].x}));
        const float maxX = std::min(static_cast<float>(fb.width) - 1.0f,
                                    std::max({p[0].x, p[1].x, p[2].x}));
        const float minY = std::max(0.0f, std::min({p[0].y, p[1].y, p[2].y}));
        const float maxY = std::min(static_cast<float>(fb.height) - 1.0f,
                                    std::max({p[0].y, p[1].y, p[2].y}));
        if (minX > maxX || minY > maxY) continue;

        const auto edge = [](const solarc::Vec2& a, const solarc::Vec2& b,
                             const solarc::Vec2& c) {
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        };
        const float area = edge(p[0], p[1], p[2]);
        if (std::fabs(area) < 1e-6f) continue;

        for (int iy = static_cast<int>(minY); iy <= static_cast<int>(maxY); ++iy) {
            for (int ix = static_cast<int>(minX); ix <= static_cast<int>(maxX); ++ix) {
                const solarc::Vec2 q{static_cast<float>(ix) + 0.5f,
                                     static_cast<float>(iy) + 0.5f};
                float w0 = edge(p[1], p[2], q) / area;
                float w1 = edge(p[2], p[0], q) / area;
                float w2 = edge(p[0], p[1], q) / area;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                // Perspective-correct weights.
                const float s = w0 * invW[0] + w1 * invW[1] + w2 * invW[2];
                if (std::fabs(s) < 1e-9f) continue;
                const float a0 = w0 * invW[0] / s;
                const float a1 = w1 * invW[1] / s;
                const float a2 = w2 * invW[2] / s;

                const float depth = a0 * v[0].clip.z + a1 * v[1].clip.z + a2 * v[2].clip.z;
                const std::size_t idx = static_cast<std::size_t>(iy) * fb.width + ix;
                if (depth >= fb.depth[idx]) continue;

                solarc::ShadingInput input;
                input.normal = {
                    a0 * v[0].normal.x + a1 * v[1].normal.x + a2 * v[2].normal.x,
                    a0 * v[0].normal.y + a1 * v[1].normal.y + a2 * v[2].normal.y,
                    a0 * v[0].normal.z + a1 * v[1].normal.z + a2 * v[2].normal.z
                };
                input.uv = {
                    a0 * v[0].uv.x + a1 * v[1].uv.x + a2 * v[2].uv.x,
                    a0 * v[0].uv.y + a1 * v[1].uv.y + a2 * v[2].uv.y
                };
                input.worldPos = {
                    a0 * v[0].world.x + a1 * v[1].world.x + a2 * v[2].world.x,
                    a0 * v[0].world.y + a1 * v[1].world.y + a2 * v[2].world.y,
                    a0 * v[0].world.z + a1 * v[1].world.z + a2 * v[2].world.z
                };

                solarc::DirectionalLight light;
                light.direction = (lightPos - input.worldPos).normalized();
                light.intensity = {1.0f, 1.0f, 1.0f, 1.0f};

                fb.color[idx] = solarc::shade(material, input, light).linear;
                fb.depth[idx] = depth;
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::string output = argc > 1 ? argv[1] : "module_c_demo.ppm";
    const int w = argc > 2 ? std::max(64, std::atoi(argv[2])) : 480;
    const int h = argc > 3 ? std::max(64, std::atoi(argv[3])) : 320;
    // mode: main | seam | ring-up | ring-down
    const std::string mode = argc > 4 ? argv[4] : "main";

    Framebuffer fb(w, h);

    solarc::Texture planetTex = solarc::makePlanetTestTexture(256, 128);
    const auto planetTexPtr = std::make_shared<solarc::Texture>(planetTex);
    solarc::Texture ringTex = solarc::makeRingBandTexture();
    const auto ringTexPtr = std::make_shared<solarc::Texture>(ringTex);

    solarc::Vec3 eye{0.0f, 4.2f, 9.5f};
    solarc::Vec3 target{0.8f, 0.0f, 0.0f};
    float fov = solarc::kPi / 4.0f;

    if (mode == "seam") {
        // Look straight down the +X meridian where u = 0 / u = 1 meet.
        // The checker texture makes any discontinuity at the seam visible.
        eye = {4.0f, 0.0f, 0.02f};
        target = {0.0f, 0.0f, 0.0f};
        fov = solarc::kPi / 3.2f;
    } else if (mode == "ring-up" || mode == "ring-down") {
        // Look at the ring from above / below the XZ plane.
        const float sign = (mode == "ring-up") ? 1.0f : -1.0f;
        eye = {0.6f, sign * 3.2f, 3.0f};
        target = {0.0f, 0.0f, 0.0f};
        fov = solarc::kPi / 3.6f;
    }

    const solarc::Mat4 view = solarc::Mat4::lookAt(eye, target, {0.0f, 1.0f, 0.0f});
    const solarc::Mat4 proj = solarc::Mat4::perspective(fov,
        static_cast<float>(w) / static_cast<float>(h), 0.1f, 100.0f);
    const solarc::Mat4 viewProj = proj * view;

    if (mode == "seam") {
        // Seam check: one textured sphere, light from the upper right.
        const solarc::Mesh sphere = solarc::buildSphere(
            solarc::makeSphereParams(1.4f, solarc::SphereDetail::High, "seam-check"));
        solarc::Texture check = solarc::makeCheckerTexture(16);
        check.wrapU = solarc::WrapMode::Repeat;
        check.wrapV = solarc::WrapMode::Clamp;
        check.filter = solarc::FilterMode::Bilinear;
        const auto checkPtr = std::make_shared<solarc::Texture>(check);
        drawMesh(fb, sphere, solarc::Mat4::identity(), viewProj,
                 solarc::makePlanetMaterial(checkPtr, 0.10f), {2.0f, 1.5f, 2.5f});
    } else if (mode == "ring-up" || mode == "ring-down") {
        // Two-sided ring check: the light follows the viewing hemisphere so the
        // visible face should be lit. A missing underside would be black here.
        const solarc::Mesh ring = solarc::buildRing({1.0f, 2.0f, 8, 128, true, "two-sided-ring"});
        const float sign = (mode == "ring-up") ? 1.0f : -1.0f;
        drawMesh(fb, ring, solarc::Mat4::identity(), viewProj,
                 solarc::makeRingMaterial(ringTexPtr, true), {1.5f, sign * 4.0f, 1.0f});
    } else {
        const solarc::Mesh sun = solarc::buildSphere({1.15f, 48, 24, false, "sun"});
        const solarc::Mesh planet = solarc::buildSphere({0.85f, 48, 24, false, "demo-planet"});
        const solarc::Mesh ring = solarc::buildRing({1.15f, 2.05f, 8, 128, true, "demo-ring"});
        const solarc::Mesh orbit = solarc::buildOrbitRibbon(3.4f, 0.025f, 128, "demo-orbit");

        drawMesh(fb, orbit, solarc::Mat4::identity(), viewProj,
                 solarc::makeOrbitMaterial({0.10f, 0.16f, 0.28f, 1.0f}), {0.0f, 0.0f, 0.0f});
        drawMesh(fb, sun, solarc::Mat4::identity(), viewProj,
                 solarc::makeSunMaterial(), {0.0f, 0.0f, 0.0f});

        const solarc::Mat4 tilt = solarc::Mat4::rotationX(0.42f);
        const solarc::Mat4 planetModel = solarc::Mat4::translation(3.4f, 0.0f, 0.0f) * tilt;
        drawMesh(fb, planet, planetModel, viewProj,
                 solarc::makePlanetMaterial(planetTexPtr, 0.055f), {0.0f, 0.0f, 0.0f});
        drawMesh(fb, ring, planetModel, viewProj,
                 solarc::makeRingMaterial(ringTexPtr, true), {0.0f, 0.0f, 0.0f});
    }

    solarc::Texture image;
    image.width = w;
    image.height = h;
    image.pixels = fb.color;
    image.wrapU = solarc::WrapMode::Clamp;
    image.wrapV = solarc::WrapMode::Clamp;

    // Dark space background for pixels never touched by any triangle.
    for (solarc::Rgba& px : image.pixels) {
        if (px.r == 0.0f && px.g == 0.0f && px.b == 0.0f && px.a == 0.0f) {
            px = solarc::srgbToLinear({0.035f, 0.045f, 0.075f, 1.0f});
        }
    }

    if (!solarc::savePPM(output, image)) {
        std::printf("failed to write %s\n", output.c_str());
        return 1;
    }
    std::printf("wrote %s (%d x %d)\n", output.c_str(), w, h);
    return 0;
}
