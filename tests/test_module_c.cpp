// Module C self-checks. Run: test_module_c
// Covers: sphere seam/normals/triangle skipping, ring winding, UV sampling,
// wrap continuity, sRGB conversion, and shading branches.
#include "solarc/geometry.h"
#include "solarc/shading.h"
#include "solarc/texture.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const std::string& label) {
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("  FAIL: %s\n", label.c_str());
    }
}

bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps;
}

void testSphere() {
    std::printf("[sphere]\n");
    const solarc::SphereParams p{2.0f, 16, 8};
    const solarc::Mesh mesh = solarc::buildSphere(p);

    check(mesh.vertices.size() == static_cast<std::size_t>(17) * 9, "vertex count = (su+1)(sv+1)");
    // 2 triangles per quad unless a pole collapses one of them.
    check(mesh.indices.size() / 3 == 16u * 8u * 2u - 16u * 2u, "pole triangles skipped");

    bool radiusOk = true, normalOk = true;
    for (const solarc::Vertex& v : mesh.vertices) {
        radiusOk &= near(v.position.length(), 2.0f, 1e-3f);
        normalOk &= near(v.normal.length(), 1.0f, 1e-3f);
    }
    check(radiusOk, "all vertices on radius 2 sphere");
    check(normalOk, "all normals unit length");

    // Seam: first and last column share position and normal, differ only in u.
    bool seamOk = true;
    for (int iy = 0; iy <= 8; ++iy) {
        const std::size_t a = static_cast<std::size_t>(iy) * 17;
        const std::size_t b = a + 16;
        const solarc::Vertex& va = mesh.vertices[a];
        const solarc::Vertex& vb = mesh.vertices[b];
        seamOk &= near(va.position.x, vb.position.x) && near(va.position.y, vb.position.y) &&
                  near(va.position.z, vb.position.z);
        seamOk &= near(va.uv.x, 0.0f) && near(vb.uv.x, 1.0f) && near(va.uv.y, vb.uv.y);
    }
    check(seamOk, "longitude seam duplicated correctly");

    // Outward winding: cross of the first non-degenerate triangle must point away
    // from the origin (dot with centroid > 0).
    bool windingOk = true;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const solarc::Vertex& a = mesh.vertices[mesh.indices[i]];
        const solarc::Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const solarc::Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        const solarc::Vec3 ab = b.position - a.position;
        const solarc::Vec3 ac = c.position - a.position;
        const solarc::Vec3 n = ab.cross(ac);
        const solarc::Vec3 centroid = (a.position + b.position + c.position) / 3.0f;
        windingOk &= n.dot(centroid) > 0.0f;
    }
    check(windingOk, "all triangle windings face outward");

    const solarc::MeshStats stats = solarc::getMeshStats(mesh);
    check(stats.vertexCount == mesh.vertices.size() && stats.triangleCount == mesh.indices.size() / 3,
          "MeshStats reports expected values");

    // A+C shared precision tiers.
    const solarc::SphereParams low = solarc::makeSphereParams(1.0f, solarc::SphereDetail::Low);
    const solarc::SphereParams med = solarc::makeSphereParams(1.0f, solarc::SphereDetail::Medium);
    const solarc::SphereParams high = solarc::makeSphereParams(1.0f, solarc::SphereDetail::High);
    check(low.segmentsU == 16 && low.segmentsV == 8, "Low precision tier = 16x8");
    check(med.segmentsU == 32 && med.segmentsV == 16, "Medium precision tier = 32x16");
    check(high.segmentsU == 64 && high.segmentsV == 32, "High precision tier = 64x32");
    check(low.radius == 1.0f && high.radius == 1.0f, "precision tiers preserve radius");
}

void testRing() {
    std::printf("[ring]\n");
    const solarc::Mesh mesh = solarc::buildRing({1.0f, 2.0f, 3, 8, true});
    check(mesh.vertices.size() == 2u * 4u * 9u, "double-sided vertex count");

    bool upper = true, lower = true, radiusOk = true, uvOk = true;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const solarc::Vertex& v = mesh.vertices[i];
        const float r = std::hypot(v.position.x, v.position.z);
        radiusOk &= r >= 0.999f && r <= 2.001f;
        uvOk &= v.uv.x >= 0.0f && v.uv.x <= 1.0f && v.uv.y >= 0.0f && v.uv.y <= 1.0f;
        if (i < 36) upper &= near(v.normal.y, 1.0f);
        else lower &= near(v.normal.y, -1.0f);
    }
    check(radiusOk, "ring radii within [inner, outer]");
    check(uvOk, "ring UVs in [0,1]");
    check(upper, "upper face normals +Y");
    check(lower, "lower face normals -Y");

    // Upper triangles must have +Y geometric normal.
    bool windingOk = true;
    for (std::size_t i = 0; i < 3u * 3u * 6u; i += 3) {
        const solarc::Vertex& a = mesh.vertices[mesh.indices[i]];
        const solarc::Vertex& b = mesh.vertices[mesh.indices[i + 1]];
        const solarc::Vertex& c = mesh.vertices[mesh.indices[i + 2]];
        windingOk &= (b.position - a.position).cross(c.position - a.position).y > 0.0f;
    }
    check(windingOk, "upper ring winding is CCW from +Y");

    const solarc::Mesh orbit = solarc::buildOrbitRibbon(5.0f, 0.04f, 32);
    check(orbit.vertices.size() == 2u * 2u * 33u, "orbit ribbon is a narrow two-sided ring");
}

void testTexture() {
    std::printf("[texture]\n");
    check(near(solarc::srgbChannelToLinear(1.0f), 1.0f), "sRGB 1 -> linear 1");
    check(solarc::srgbChannelToLinear(0.5f) < 0.5f, "sRGB 0.5 darkens in linear");
    check(near(solarc::linearChannelToSrgb(solarc::srgbChannelToLinear(0.37f)), 0.37f, 2e-3f),
          "sRGB round trip");

    solarc::Texture checker = solarc::makeCheckerTexture(8);
    checker.filter = solarc::FilterMode::Nearest;
    const solarc::Rgba a = solarc::sampleTexture(checker, 0.01f, 0.01f);
    const solarc::Rgba b = solarc::sampleTexture(checker, 1.01f, 0.01f);
    check(near(a.r, b.r) && near(a.g, b.g), "Repeat wrapping is continuous across u = 0/1");

    checker.wrapU = solarc::WrapMode::Mirror;
    const solarc::Rgba m0 = solarc::sampleTexture(checker, 0.02f, 0.01f);
    const solarc::Rgba m1 = solarc::sampleTexture(checker, -0.02f, 0.01f);
    check(near(m0.r, m1.r), "Mirror wrapping is symmetric around zero");

    solarc::Texture bilinear = solarc::makePlanetTestTexture(32, 16);
    bilinear.wrapU = solarc::WrapMode::Repeat;
    bilinear.wrapV = solarc::WrapMode::Clamp;
    const solarc::Rgba seamA = solarc::sampleTexture(bilinear, 0.0f, 0.5f);
    const solarc::Rgba seamB = solarc::sampleTexture(bilinear, 1.0f, 0.5f);
    check(near(seamA.r, seamB.r, 5e-3f) && near(seamA.b, seamB.b, 5e-3f),
          "bilinear seam colors match at u = 0 and u = 1");

    const solarc::Rgba inside = solarc::sampleTexture(bilinear, 0.5f, -0.5f);
    const solarc::Rgba edge = solarc::sampleTexture(bilinear, 0.5f, 0.0f);
    check(near(inside.r, edge.r, 5e-3f), "Clamp wrapping holds the border column");
}

void testShading() {
    std::printf("[shading]\n");
    solarc::Texture tex = solarc::makePlanetTestTexture(8, 4);
    tex.filter = solarc::FilterMode::Nearest;
    const solarc::Material planet = solarc::makePlanetMaterial(
        std::make_shared<solarc::Texture>(tex), 0.05f);

    solarc::DirectionalLight light;
    light.direction = {0.0f, 0.0f, 1.0f};
    light.intensity = {1.0f, 1.0f, 1.0f, 1.0f};

    solarc::ShadingInput input;
    input.normal = {0.0f, 0.0f, 1.0f};   // facing the light
    input.uv = {0.5f, 0.5f};
    const solarc::ShadedColor lit = solarc::shade(planet, input, light);

    input.normal = {0.0f, 0.0f, -1.0f};  // facing away
    const solarc::ShadedColor dark = solarc::shade(planet, input, light);
    check(lit.linear.r > dark.linear.r, "Lambert front is brighter than back");
    check(dark.linear.r > 0.0f, "ambient term keeps the dark side non-zero");

    input.normal = {0.0f, 0.0f, 1.0f};
    input.normal = input.normal.normalized();
    const solarc::Vec3 unnormalized{0.0f, 0.0f, 5.0f};
    input.normal = unnormalized;
    const solarc::ShadedColor normalized = solarc::shade(planet, input, light);
    check(near(normalized.linear.r, lit.linear.r, 1e-5f), "normal is normalized inside shade()");

    const solarc::Material sun = solarc::makeSunMaterial();
    const solarc::ShadedColor sunFront = solarc::shade(sun, input, light);
    input.normal = {0.0f, 0.0f, -1.0f};
    const solarc::ShadedColor sunBack = solarc::shade(sun, input, light);
    check(near(sunFront.linear.r, sunBack.linear.r), "sun emissive ignores light direction");
    check(sunFront.linear.r >= sunFront.linear.b, "sun color is warm");

    const solarc::Material orbit = solarc::makeOrbitMaterial({0.2f, 0.4f, 0.8f, 1.0f});
    input.normal = {0.0f, 0.0f, -1.0f};
    const solarc::ShadedColor orbitColor = solarc::shade(orbit, input, light);
    check(near(orbitColor.linear.b, 0.8f), "unlit material returns base color directly");
}

} // namespace

int main() {
    std::printf("=== Module C self-checks ===\n");
    testSphere();
    testRing();
    testTexture();
    testShading();
    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
