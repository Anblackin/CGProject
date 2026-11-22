#pragma once

#include "planet_sandbox/texture.h"

namespace planet_sandbox {

// Map only baseline assets; sculpting and picking stay in geometric coordinates.
Vec2 solar_visual_uv(BodyKind kind, double u, double v, const DesignOverrides& design);
double solar_storm_mask(BodyKind kind, double source_u, double source_v);

struct VisualDetailMap {
    int width = 0, height = 0;
    std::vector<Vec2> gradient;
};

// Residual detail excludes mesh-scale relief to avoid lighting the same slope twice.
const VisualDetailMap& solar_detail_map(BodyKind kind, SolarShapeLayer layer);
Vec2 sample_visual_detail(const VisualDetailMap& map, double u, double v);

struct VisualVector { double x, y, z; };
VisualVector perturb_visual_normal(VisualVector normal, VisualVector radial, VisualVector pole,
                                    double longitude_slope, double latitude_slope);
// Light and plane normal must be unit vectors; point and radius share one scale.
bool planet_blocks_light(VisualVector relative_point, VisualVector light, double radius);
double ring_plane_radius(VisualVector relative_point, VisualVector light, VisualVector normal);

} // namespace planet_sandbox
