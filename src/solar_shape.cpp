#include "planet_sandbox/texture.h"
#include "planet_sandbox/sculpt.h"
#include "planet_sandbox/planet_visual.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace planet_sandbox {
namespace {

constexpr double pi = 3.14159265358979323846;

double polar_fade(double v) {
    if (v <= 0.0 || v >= 1.0) return 0.0;
    const double factor = std::clamp(std::sin(pi * v) / 0.2, 0.0, 1.0);
    return factor * factor * (3.0 - 2.0 * factor);
}

double mercury_basin_mask(double u, double v) {
    const double center = solar_height_sample(BodyKind::Mercury, u, v);
    double surroundings = 0.0;
    for (const auto& offset : std::array<std::array<double, 2>, 8>{{
             {{-0.025, 0.0}}, {{0.025, 0.0}}, {{0.0, -0.05}}, {{0.0, 0.05}},
             {{-0.018, -0.035}}, {{0.018, -0.035}},
             {{-0.018, 0.035}}, {{0.018, 0.035}}
         }}) {
        surroundings += solar_height_sample(BodyKind::Mercury, u + offset[0], v + offset[1]);
    }
    return std::clamp((surroundings / 8.0 - center) * 5.0, 0.0, 1.0);
}

double jupiter_belt_sample(double v) {
    static const auto profile = [] {
        std::array<double, 129> values{};
        for (int row = 0; row <= 128; ++row) {
            for (int col = 0; col < 32; ++col) {
                values[row] += solar_height_sample(BodyKind::Jupiter,
                                                   col / 32.0, row / 128.0) / 32.0;
            }
        }
        return values;
    }();
    const double position = std::clamp(v, 0.0, 1.0) * 128.0;
    const int row = static_cast<int>(position);
    const int next = std::min(row + 1, 128);
    return profile[row] * (1.0 - (position - row)) + profile[next] * (position - row);
}

double venus_cloud_radius(double u, double v, const DesignOverrides& design) {
    const double height = solar_height_sample(BodyKind::Venus, u, v);
    return 1.055 + (height - 0.5) * 0.012 * design.relief * polar_fade(v) +
           sculpt_offset(design, SolarShapeLayer::Visible, u, v);
}

} // namespace

double solar_shape_multiplier(BodyKind kind, double u, double v,
                              const DesignOverrides& design, SolarShapeLayer layer) {
    if (layer == SolarShapeLayer::VenusRadar) {
        if (kind != BodyKind::Venus) {
            throw std::invalid_argument("Radar surface is available only for Venus");
        }
        const double radar = solar_venus_radar_height_sample(u, v);
        const double radius = 1.0 + (radar - 0.5) * 0.014 * design.radar_relief * polar_fade(v) +
                              sculpt_offset(design, layer, u, v);
        // Keep the radar mesh inside the cloud deck at opposite brush extremes.
        return std::min(radius, venus_cloud_radius(u, v, design) - 0.008);
    }
    if (kind == BodyKind::Venus) {
        return venus_cloud_radius(u, v, design);
    }
    const auto source = solar_visual_uv(kind, u, v, design);
    const double height = solar_height_sample(kind, source.x, source.y);
    double radius = solar_relief_multiplier(kind, height, v, design.relief);
    if (kind == BodyKind::Mercury) {
        radius -= 0.018 * design.crater_depth * mercury_basin_mask(u, v) * polar_fade(v);
    }
    if (kind == BodyKind::Jupiter) {
        radius += 0.012 * design.belt_relief *
                  (jupiter_belt_sample(source.y) - 0.5) * polar_fade(v);
    }
    if (kind == BodyKind::Jupiter || kind == BodyKind::Neptune) {
        radius += 0.008 * design.storm_relief * std::min(1.0, design.storm_scale) *
                  solar_storm_mask(kind, source.x, source.y) * polar_fade(v);
    }
    return radius + sculpt_offset(design, layer, u, v);
}

} // namespace planet_sandbox
