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
struct SphereMeshDetail { int latitude_steps=48,coarse_latitude_steps=48;double morph=1; };
SphereMeshDetail sphere_mesh_detail(double projected_radius);
double mesh_shape_multiplier(BodyKind kind,double u,double v,const DesignOverrides& design,
                             SolarShapeLayer layer,const SphereMeshDetail& detail);
// Signed light/view cosines identify the reflected and transmitted sides of a thin ring.
double ring_light_transmission(double opacity,double light_cosine);
double ring_reflectance(double opacity,double light_cosine,double view_cosine,double sunlight_visibility=1);
struct CloudReflectance { double alpha=0,brightness=0; };
// Optical-depth cues for the existing cloud shell, not a volume weather solver.
double cloud_light_transmission(double mask,double opacity,double light_cosine);
CloudReflectance cloud_reflectance(double mask,double opacity,double radial_light_cosine,
                                   double detail_light_cosine,double view_cosine,double light_view_cosine);
struct RockReflectance { double diffuse=0,specular=0; };
// Unit surface, light and view vectors; returns bounded artistic lighting factors.
RockReflectance rock_reflectance(VisualVector normal,VisualVector light,VisualVector view,double roughness);
// Ocean coverage excludes rock response; ice coverage selects the smoother ice finish.
RockReflectance rock_surface_reflectance(VisualVector normal,VisualVector light,VisualVector view,
                                         double land_roughness,double ocean_coverage,double ice_coverage);
struct BodySurfaceRotation {
    double spin_cos=1,spin_sin=0,tilt_cos=1,tilt_sin=0;
    bool uranus=false;
    VisualVector to_world(VisualVector local) const;
    VisualVector to_local(VisualVector world) const;
};
BodySurfaceRotation body_surface_rotation(BodyKind kind,std::uint32_t seed,double axis_tilt_offset,double time);
VisualVector perturb_visual_normal(VisualVector normal, VisualVector radial, VisualVector pole,
                                    double longitude_slope, double latitude_slope);
// Light and plane normal must be unit vectors; point and radius share one scale.
bool planet_blocks_light(VisualVector relative_point, VisualVector light, double radius);
double ring_plane_radius(VisualVector relative_point, VisualVector light, VisualVector normal);

} // namespace planet_sandbox
