#pragma once
#include "planet_sandbox/simulation.h"
#include "planet_sandbox/planet_visual.h"
#include <array>

namespace planet_sandbox {
// Visual ejecta have no mass and never participate in collision or picking.
struct ImpactSprite {
    double x=0, y=0, z=0, radius=0, opacity=0;
    std::array<double,3> color{};
    int body_index=-1;
};
constexpr double impact_visual_lifetime=1.5;
std::vector<ImpactSprite> impact_sprites(const Project& project, const SceneSnapshot& snapshot);
struct ImpactSurfacePatch {
    VisualVector local_direction{};
    double cosine_radius=1, heat=0, darkening=0;
};
struct ImpactSurfaceResponse {
    std::array<double,3> emission{};
    double darkening=0;
};
constexpr double impact_surface_lifetime=6.0;
std::vector<ImpactSurfacePatch> impact_surface_patches(const BodySpec& body, const SceneSnapshot& snapshot,
    std::optional<BodyKind> rendered_kind=std::nullopt);
ImpactSurfaceResponse sample_impact_surface(const std::vector<ImpactSurfacePatch>& patches, double u, double v);
}
