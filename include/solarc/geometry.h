// Module C - configurable procedural geometry.
// All meshes use the shared Vertex / Mesh structs from types.h so that module B's
// rasterizer can consume them without conversion.
#pragma once

#include "types.h"

namespace solarc {

// Equirectangular UV sphere. The longitude seam is intentionally DUPLICATED
// (segmentsU + 1 columns) so u = 0 and u = 1 are distinct vertices and the
// sampler's Repeat wrapping produces a continuous texture across the seam.
struct SphereParams {
    float radius = 1.0f;
    int segmentsU = 48;   // longitude, >= 3
    int segmentsV = 24;   // latitude,  >= 2
    // v = 0 at the north pole, v = 1 at the south pole (top-down image rows).
    bool flipV = false;
    std::string name = "sphere";
};

Mesh buildSphere(const SphereParams& p);

// Precision tiers agreed for the A+C collaboration group: module A's planet
// parameter table picks a tier from display radius / camera distance, and
// module C turns it into concrete SphereParams. Low = 16x8, Medium = 32x16,
// High = 64x32. Keeping this here avoids A hard-coding segment counts.
enum class SphereDetail { Low, Medium, High };

SphereParams makeSphereParams(float radius, SphereDetail detail,
                              const std::string& name = "sphere");

// Annulus lying in the XZ plane (y = 0). Saturn's rings are built from this.
// Both faces are emitted when doubleSided is true: the +Y face has normal (0,1,0)
// and CCW winding seen from +Y; the -Y face has normal (0,-1,0) and reversed
// winding, so module B can keep ordinary back-face culling enabled.
struct RingParams {
    float innerRadius = 1.4f;
    float outerRadius = 2.4f;
    int radialSegments = 6;
    int angularSegments = 128;
    bool doubleSided = true;
    // u runs around the circumference, v runs from inner (0) to outer (1).
    std::string name = "ring";
};

Mesh buildRing(const RingParams& p);

// Thin orbit guide line: a narrow annulus centered on orbitRadius.
Mesh buildOrbitRibbon(float orbitRadius, float width, int angularSegments,
                      const std::string& name = "orbit");

// A single large triangle in world space, used by module B for raster tests.
Mesh buildTestTriangle();

struct MeshStats {
    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
};

MeshStats getMeshStats(const Mesh& mesh);

} // namespace solarc
