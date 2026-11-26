#pragma once

#include "planet_sandbox/project.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace planet_sandbox {

enum class AssetLayer { Color, Height, Clouds, Ring, VenusSurface };
enum class SolarShapeLayer { Visible, VenusRadar };

struct Texture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

Texture load_texture(const std::string& path);
std::array<double, 4> sample_texture(const Texture& texture, double u, double v,
                                     bool wrap_u = true);
// Selective samplers retain the RGBA sampler's filtering and validation semantics.
double sample_texture_channel(const Texture& texture,double u,double v,int channel,bool wrap_u=true);
double sample_texture_rgb_mean(const Texture& texture,double u,double v,bool wrap_u=true);
std::string solar_asset_path(BodyKind kind, AssetLayer layer);
const Texture& solar_texture(BodyKind kind, AssetLayer layer);
double solar_height_sample(BodyKind kind, double u, double v);
double solar_venus_radar_height_sample(double u, double v);
double solar_relief_multiplier(BodyKind kind, double height, double v, double relief);
double solar_shape_multiplier(BodyKind kind, double u, double v,
                              const DesignOverrides& design,
                              SolarShapeLayer layer = SolarShapeLayer::Visible);

} // namespace planet_sandbox
