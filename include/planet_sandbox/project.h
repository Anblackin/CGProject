#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace planet_sandbox {

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

struct Appearance {
    std::uint32_t seed = 1;
    std::string primary = "#6dbec0";
    std::string secondary = "#243e72";
    double bands = 6.0;
    double terrain = 0.5;
    double clouds = 0.2;
    double ring_width = 0.0;
};

enum class BodyKind { Custom, Sun, Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune };

struct DesignOverrides {
    std::vector<float> sculpt_visible;
    std::vector<float> sculpt_radar;
    double relief = 1.0;
    double crater_depth = 1.0;
    double radar_relief = 1.0;
    double belt_relief = 1.0;
    double storm_relief = 1.0;
    double color_shift = 0.0;
    double cloud_opacity = 0.35;
    double dust_opacity = 0.0;
    double ice_extent = 0.0;
    double band_contrast = 1.0;
    double storm_scale = 1.0;
    double ring_width = 1.0;
    double granulation = 1.0;
    double sunspot_strength = 1.0;
    double cassini_gap = 1.0;
    double axis_tilt_offset = 0.0;
    double granule_scale = 1.0;
    double cloud_motion = 1.0;
    double ring_tilt_offset = 0.0;
    double surface_detail = 1.0;
    double atmosphere_density = 1.0;
    double cloud_altitude = 0.018;
    double cloud_shadow = 0.65;
    double band_warp = 0.0;
    double storm_longitude = 0.37;
    double storm_latitude = 0.61;
    double storm_aspect = 1.0;
};

struct BodySpec {
    std::string id;
    std::string name;
    double mass = 1.0;
    double radius = 0.1;
    Vec2 position;
    Vec2 velocity;
    Appearance appearance;
    BodyKind kind = BodyKind::Custom;
    double display_radius = 0.0;
    DesignOverrides design;
};

struct Project {
    std::vector<BodySpec> bodies;
    double view_extent = 5.0;
    double display_scale = 2.0;
    bool solar_system = false;
};

Project read_project(std::istream& input);
void write_project(std::ostream& output, const Project& project);
void validate_project(const Project& project);

} // namespace planet_sandbox
