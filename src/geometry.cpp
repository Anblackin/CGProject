#include "solarc/geometry.h"

#include <cmath>
#include <limits>

namespace solarc {

Mesh buildSphere(const SphereParams& p) {
    Mesh mesh;
    mesh.name = p.name;

    const int su = p.segmentsU < 3 ? 3 : p.segmentsU;
    const int sv = p.segmentsV < 2 ? 2 : p.segmentsV;
    const float r = p.radius;

    mesh.vertices.reserve(static_cast<std::size_t>(su + 1) * static_cast<std::size_t>(sv + 1));

    for (int iy = 0; iy <= sv; ++iy) {
        const float v = static_cast<float>(iy) / static_cast<float>(sv);
        const float theta = v * kPi;                  // 0 at north pole
        const float sinT = std::sin(theta);
        const float cosT = std::cos(theta);

        for (int ix = 0; ix <= su; ++ix) {
            const float u = static_cast<float>(ix) / static_cast<float>(su);
            const float phi = u * kTwoPi;

            Vertex vert;
            // XZ orbital plane, Y up. Seam vertices (ix = su) coincide geometrically
            // with ix = 0 but carry u = 1, which is the whole point of the duplication.
            vert.position = {r * sinT * std::cos(phi), r * cosT, r * sinT * std::sin(phi)};
            vert.normal = sinT > 1e-8f
                ? Vec3{sinT * std::cos(phi), cosT, sinT * std::sin(phi)}
                : Vec3{0.0f, cosT, 0.0f};
            vert.normal = vert.normal.normalized();
            vert.uv = {u, p.flipV ? 1.0f - v : v};
            mesh.vertices.push_back(vert);
        }
    }

    const std::size_t row = static_cast<std::size_t>(su) + 1;
    mesh.indices.reserve(static_cast<std::size_t>(su) * static_cast<std::size_t>(sv) * 6);

    // Vertex positions are shared; skip zero-area triangles at the poles.
    const auto samePoint = [&](std::size_t a, std::size_t b) {
        const Vec3& va = mesh.vertices[a].position;
        const Vec3& vb = mesh.vertices[b].position;
        const float dx = va.x - vb.x, dy = va.y - vb.y, dz = va.z - vb.z;
        return (dx * dx + dy * dy + dz * dz) < 1e-12f;
    };

    for (int iy = 0; iy < sv; ++iy) {
        for (int ix = 0; ix < su; ++ix) {
            const std::size_t i00 = static_cast<std::size_t>(iy) * row + static_cast<std::size_t>(ix);
            const std::size_t i01 = i00 + 1;              // +phi
            const std::size_t i10 = i00 + row;            // +theta (toward south)
            const std::size_t i11 = i10 + 1;

            // Winding: (i00, i01, i10) gives an outward normal for a right-handed
            // system (verified at theta = pi/2, phi = 0).
            if (!samePoint(i00, i01) && !samePoint(i00, i10)) {
                mesh.indices.push_back(static_cast<uint32_t>(i00));
                mesh.indices.push_back(static_cast<uint32_t>(i01));
                mesh.indices.push_back(static_cast<uint32_t>(i10));
            }
            if (!samePoint(i01, i11) && !samePoint(i01, i10) && !samePoint(i11, i10)) {
                mesh.indices.push_back(static_cast<uint32_t>(i01));
                mesh.indices.push_back(static_cast<uint32_t>(i11));
                mesh.indices.push_back(static_cast<uint32_t>(i10));
            }
        }
    }
    return mesh;
}

SphereParams makeSphereParams(float radius, SphereDetail detail, const std::string& name) {
    SphereParams p;
    p.radius = radius;
    p.name = name;
    switch (detail) {
    case SphereDetail::Low:
        p.segmentsU = 16; p.segmentsV = 8;
        break;
    case SphereDetail::Medium:
        p.segmentsU = 32; p.segmentsV = 16;
        break;
    case SphereDetail::High:
    default:
        p.segmentsU = 64; p.segmentsV = 32;
        break;
    }
    return p;
}

Mesh buildRing(const RingParams& p) {
    Mesh mesh;
    mesh.name = p.name;

    const int rs = p.radialSegments < 1 ? 1 : p.radialSegments;
    const int as = p.angularSegments < 3 ? 3 : p.angularSegments;
    const float ri = p.innerRadius > 0.0f ? p.innerRadius : 0.01f;
    const float ro = p.outerRadius > ri ? p.outerRadius : ri + 0.01f;

    const std::size_t ringSize = static_cast<std::size_t>(as) + 1;
    const std::size_t faceSize = ringSize * static_cast<std::size_t>(rs + 1);
    const std::size_t total = p.doubleSided ? faceSize * 2 : faceSize;
    mesh.vertices.reserve(total);

    // Helper adds one annulus face with the requested normal direction.
    const auto addFace = [&](bool upperFace) {
        const float ny = upperFace ? 1.0f : -1.0f;
        const std::size_t base = mesh.vertices.size();

        for (int ir = 0; ir <= rs; ++ir) {
            const float t = static_cast<float>(ir) / static_cast<float>(rs);
            const float radius = ri + (ro - ri) * t;
            for (int ia = 0; ia <= as; ++ia) {
                const float u = static_cast<float>(ia) / static_cast<float>(as);
                const float phi = u * kTwoPi;
                Vertex vert;
                vert.position = {radius * std::cos(phi), 0.0f, radius * std::sin(phi)};
                vert.normal = {0.0f, ny, 0.0f};
                vert.uv = {u, t};
                mesh.vertices.push_back(vert);
            }
        }

        for (int ir = 0; ir < rs; ++ir) {
            for (int ia = 0; ia < as; ++ia) {
                const std::size_t i00 = base + static_cast<std::size_t>(ir) * ringSize + static_cast<std::size_t>(ia);
                const std::size_t i01 = i00 + 1;                 // +phi
                const std::size_t i10 = i00 + ringSize;           // +radius
                const std::size_t i11 = i10 + 1;

                if (upperFace) {
                    // cross(dPhi, dRadius) = +Y
                    mesh.indices.push_back(static_cast<uint32_t>(i00));
                    mesh.indices.push_back(static_cast<uint32_t>(i01));
                    mesh.indices.push_back(static_cast<uint32_t>(i10));
                    mesh.indices.push_back(static_cast<uint32_t>(i01));
                    mesh.indices.push_back(static_cast<uint32_t>(i11));
                    mesh.indices.push_back(static_cast<uint32_t>(i10));
                } else {
                    // reversed winding for the underside
                    mesh.indices.push_back(static_cast<uint32_t>(i00));
                    mesh.indices.push_back(static_cast<uint32_t>(i10));
                    mesh.indices.push_back(static_cast<uint32_t>(i01));
                    mesh.indices.push_back(static_cast<uint32_t>(i01));
                    mesh.indices.push_back(static_cast<uint32_t>(i10));
                    mesh.indices.push_back(static_cast<uint32_t>(i11));
                }
            }
        }
    };

    addFace(true);
    if (p.doubleSided) addFace(false);
    return mesh;
}

Mesh buildOrbitRibbon(float orbitRadius, float width, int angularSegments,
                      const std::string& name) {
    const float halfWidth = width > 0.0f ? width * 0.5f : 0.01f;
    RingParams rp;
    rp.innerRadius = orbitRadius - halfWidth;
    rp.outerRadius = orbitRadius + halfWidth;
    rp.radialSegments = 1;
    rp.angularSegments = angularSegments > 0 ? angularSegments : 96;
    rp.doubleSided = true;
    rp.name = name;
    return buildRing(rp);
}

Mesh buildTestTriangle() {
    Mesh mesh;
    mesh.name = "test-triangle";
    // Slightly in front of the camera used in demo_render; CCW seen from +Z.
    mesh.vertices.push_back({{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
    mesh.vertices.push_back({{ 0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}});
    mesh.vertices.push_back({{ 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}});
    mesh.indices = {0, 1, 2};
    return mesh;
}

MeshStats getMeshStats(const Mesh& mesh) {
    MeshStats s;
    s.vertexCount = mesh.vertices.size();
    s.triangleCount = mesh.indices.size() / 3;
    return s;
}

} // namespace solarc
