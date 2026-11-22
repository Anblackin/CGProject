#include "planet_sandbox/planet_visual.h"
#include "planet_sandbox/solar_system.h"
#include "planet_sandbox/texture.h"
#include "planet_sandbox/sculpt.h"
#include "planet_sandbox/renderer.h"
#include <nlohmann/json.hpp>
#include <cassert>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <limits>
using namespace planet_sandbox;

static void test_visual_design_roundtrip_defaults_and_validation() {
    auto project = make_solar_system_project();
    auto& earth = project.bodies[3].design;
    earth.surface_detail = 1.8; earth.atmosphere_density = 0.6;
    earth.cloud_altitude = 0.035; earth.cloud_shadow = 0.9;
    auto& jupiter = project.bodies[5].design;
    jupiter.band_warp = 0.6; jupiter.storm_longitude = 0.7;
    jupiter.storm_latitude = 0.4; jupiter.storm_aspect = 1.5;
    std::ostringstream output; write_project(output, project);
    std::istringstream input(output.str());
    const auto restored = read_project(input);
    assert(restored.bodies[3].design.surface_detail == 1.8);
    assert(restored.bodies[3].design.atmosphere_density == 0.6);
    assert(restored.bodies[3].design.cloud_altitude == 0.035);
    assert(restored.bodies[3].design.cloud_shadow == 0.9);
    assert(restored.bodies[5].design.band_warp == 0.6);
    assert(restored.bodies[5].design.storm_longitude == 0.7);
    assert(restored.bodies[5].design.storm_latitude == 0.4);
    assert(restored.bodies[5].design.storm_aspect == 1.5);
    auto old = nlohmann::json::parse(output.str());
    for (auto& body : old["bodies"]) {
        for (const char* field : {"surface_detail", "atmosphere_density", "cloud_altitude", "cloud_shadow",
                                  "band_warp", "storm_longitude", "storm_latitude", "storm_aspect"}) {
            body["overrides"].erase(field);
        }
    }
    std::istringstream legacy(old.dump());
    const auto defaults = read_project(legacy);
    assert(defaults.bodies[3].design.surface_detail == 1.0);
    assert(defaults.bodies[5].design.storm_longitude == 0.37);
    assert(defaults.bodies[8].design.storm_longitude == 0.19);
    assert(defaults.bodies[8].design.storm_latitude == 0.16);
    for (double value : {-0.1, 2.1, std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid = project; invalid.bodies[3].design.atmosphere_density = value;
        bool rejected = false;
        try { validate_project(invalid); } catch (const std::invalid_argument&) { rejected = true; }
        assert(rejected);
    }
}

static void test_gas_mapping_preserves_baseline_and_wraps() {
    for (auto kind : {BodyKind::Jupiter, BodyKind::Saturn, BodyKind::Uranus, BodyKind::Neptune}) {
        auto design = baseline_design(kind);
        for (double u : {0.0, 0.2, 0.37, 0.75, 1.0}) {
            const auto uv = solar_visual_uv(kind, u, 0.61, design);
            assert(std::abs(std::remainder(uv.x - u, 1.0)) < 1e-10);
            assert(std::abs(uv.y - 0.61) < 1e-10);
        }
        design.band_warp = 1;
        design.storm_longitude = 0.98; design.storm_latitude = 0.8; design.storm_aspect = 2;
        const auto a = solar_visual_uv(kind, 0, 0.5, design);
        const auto b = solar_visual_uv(kind, 1, 0.5, design);
        assert(std::abs(std::remainder(a.x - b.x, 1.0)) < 1e-10);
        assert(std::abs(a.y - b.y) < 1e-10);
        assert(solar_visual_uv(kind, 0.4, 0.0, design).y == 0.0);
        assert(solar_visual_uv(kind, 0.4, 1.0, design).y == 1.0);
    }
    auto moved = baseline_design(BodyKind::Jupiter);
    moved.storm_longitude = 0.72; moved.storm_latitude = 0.42;
    const auto at_center = solar_visual_uv(BodyKind::Jupiter, 0.72, 0.42, moved);
    assert(std::abs(at_center.x - 0.37) < 0.005);
    assert(std::abs(at_center.y - 0.61) < 0.005);
}

static void test_moving_storm_moves_relief_and_preserves_sculpt_space() {
    for (auto kind : {BodyKind::Jupiter, BodyKind::Neptune}) {
        auto moved = baseline_design(kind);
        moved.storm_longitude = 0.72; moved.storm_latitude = 0.42;
        auto no_storm = moved; no_storm.storm_relief = 0;
        const double at_new = solar_shape_multiplier(kind, 0.72, 0.42, moved) -
                              solar_shape_multiplier(kind, 0.72, 0.42, no_storm);
        assert(at_new > 0.006);
        const auto original = baseline_design(kind);
        const double at_old = solar_shape_multiplier(kind, original.storm_longitude, original.storm_latitude, moved) -
                              solar_shape_multiplier(kind, original.storm_longitude, original.storm_latitude, no_storm);
        assert(at_old < at_new * 0.05);
        moved.sculpt_visible.assign(sculpt_columns * sculpt_rows, 0.025f);
        const double sculpted = solar_shape_multiplier(kind, 0.72, 0.42, moved);
        moved.sculpt_visible.clear();
        assert(std::abs(sculpted - solar_shape_multiplier(kind, 0.72, 0.42, moved) - 0.025) < 1e-6);
    }
}

static RenderFrame closeup(Project project, int index, SamplingMode mode = SamplingMode::MSAA4) {
    auto body = project.bodies[index];
    project.view_extent = body.display_radius * (index == 6 ? 2.0 : 1.08);
    SceneSnapshot scene;
    scene.bodies.push_back({body.id, {}, {}, body.mass, body.radius});
    RenderConfig config;
    config.width = 180; config.height = 160; config.sampling = mode;
    config.camera.yaw = 0.25; config.camera.pitch = 0.12;
    return render_scene(project, scene, config);
}

static void test_optical_controls_change_visible_image_without_stealing_picking() {
    const auto baseline = make_solar_system_project();
    auto clear = baseline; clear.bodies[3].design.atmosphere_density = 0;
    auto dense = baseline; dense.bodies[3].design.atmosphere_density = 2;
    const auto a = closeup(clear, 3), b = closeup(dense, 3);
    assert(a.image.rgb != b.image.rgb);
    assert(a.body_index == b.body_index);
    assert(a.surface_uv.size() == b.surface_uv.size());
    for (std::size_t p = 0; p < a.surface_uv.size(); ++p) {
        assert(a.surface_uv[p].x == b.surface_uv[p].x && a.surface_uv[p].y == b.surface_uv[p].y);
    }
    auto smooth = baseline; smooth.bodies[4].design.surface_detail = 0;
    assert(closeup(smooth, 4).image.rgb != closeup(baseline, 4).image.rgb);
    auto high = baseline; high.bodies[3].design.cloud_altitude = 0.06;
    assert(closeup(high, 3).image.rgb != closeup(baseline, 3).image.rgb);
    auto no_shadow = baseline; no_shadow.bodies[3].design.cloud_shadow = 0;
    assert(closeup(no_shadow, 3).image.rgb != closeup(baseline, 3).image.rgb);
    auto opaque = baseline; opaque.bodies[3].design.cloud_opacity = 0;
    auto no_shadow_clear = opaque; no_shadow_clear.bodies[3].design.cloud_shadow = 0;
    assert(closeup(opaque, 3).image.rgb == closeup(no_shadow_clear, 3).image.rgb);
    const auto msaa = closeup(baseline, 3), ssaa = closeup(baseline, 3, SamplingMode::SSAA4);
    assert(msaa.shade_evaluations < ssaa.shade_evaluations);
}

static void test_scene_light_follows_sun_and_isolated_light_is_photographic() {
    auto project = make_solar_system_project();
    project.view_extent = 0.35;
    SceneSnapshot scene;
    const auto& earth = project.bodies[3];
    scene.bodies.push_back({earth.id, {}, {}, earth.mass, earth.radius});
    scene.bodies.push_back({"sun", {-20, 5}, {}, 1, 0.1});
    RenderConfig config; config.width = 100; config.height = 100;
    auto left = render_scene(project, scene, config);
    scene.bodies[1].position.x = 20;
    auto right = render_scene(project, scene, config);
    assert(left.image.rgb != right.image.rgb);
    scene.bodies.pop_back();
    auto isolated = render_scene(project, scene, config);
    assert(isolated.image.rgb != left.image.rgb);
}

static void test_atmosphere_clips_against_foreground_and_trace_matches_pixels() {
    auto project = make_solar_system_project(); project.view_extent = 0.5;
    SceneSnapshot scene;
    const auto& earth = project.bodies[3]; const auto& mars = project.bodies[4];
    scene.bodies.push_back({earth.id, {}, {}, earth.mass, earth.radius});
    scene.bodies.push_back({mars.id, {.8, 0}, {}, mars.mass, mars.radius});
    RenderConfig config; config.width = 160; config.height = 160; config.camera.yaw = 1.5707963267948966;
    project.bodies[3].design.atmosphere_density = 0;
    const auto clear = render_scene(project, scene, config);
    project.bodies[3].design.atmosphere_density = 2;
    const auto dense = render_scene(project, scene, config);
    int foreground = 0;
    for (std::size_t p = 0; p < clear.body_index.size(); ++p) {
        if (clear.body_index[p] != 4) continue;
        ++foreground;
        for (int c = 0; c < 3; ++c) assert(clear.image.rgb[p * 3 + c] == dense.image.rgb[p * 3 + c]);
    }
    assert(foreground > 200);
    scene.bodies.pop_back(); config.sampling = SamplingMode::MSAA4;
    const auto frame = render_scene(project, scene, config);
    std::size_t halo = frame.body_index.size();
    project.bodies[3].design.atmosphere_density = 0;
    const auto no_halo = render_scene(project, scene, config);
    for (std::size_t p = 0; p < frame.body_index.size(); ++p) {
        if (frame.body_index[p] == -1 && frame.image.rgb[p * 3 + 2] > no_halo.image.rgb[p * 3 + 2]) {
            halo = p; break;
        }
    }
    assert(halo < frame.body_index.size());
    project.bodies[3].design.atmosphere_density = 2;
    config.trace_x = int(halo % config.width); config.trace_y = int(halo / config.width);
    const auto traced = render_scene(project, scene, config);
    assert(traced.image.rgb == frame.image.rgb);
    for (int c = 0; c < 3; ++c) {
        double linear = 0;
        for (const auto& sample : traced.trace->samples) {
            assert(sample.body_index == -1);
            linear += sample.linear_rgb[c] / 4;
        }
        const int displayed = int(std::round(std::pow(std::clamp(linear, 0.0, 1.0), 1 / 2.2) * 255));
        assert(displayed == traced.trace->display_rgb[c]);
    }
}

static void test_shadow_geometry_and_extreme_storm_mapping() {
    assert(planet_blocks_light({0, 0, -2}, {0, 0, 1}, 1));
    assert(!planet_blocks_light({2, 0, -2}, {0, 0, 1}, 1));
    assert(!planet_blocks_light({0, 0, 2}, {0, 0, 1}, 1));
    const double q = std::sqrt(.5);
    assert(std::abs(ring_plane_radius({1, 1, 0}, {q, -q, 0}, {0, 1, 0}) - 2) < 1e-10);
    assert(ring_plane_radius({1, 1, 0}, {1, 0, 0}, {0, 1, 0}) == -1);
    assert(ring_plane_radius({1, 1, 0}, {0, 1, 0}, {0, 1, 0}) == -1);
    for (auto kind : {BodyKind::Jupiter, BodyKind::Neptune}) {
        for (double target : {.08, .92}) for (double aspect : {.5, 2.0}) for (double size : {0.0, 3.0}) {
            auto design = baseline_design(kind);
            design.storm_latitude = target; design.storm_longitude = .99;
            design.storm_aspect = aspect; design.storm_scale = size; design.band_warp = 1;
            // Every latitude line stays ordered: moving a storm cannot fold or duplicate it.
            for (double u : {.01, .3, .7, .99}) {
                double previous = -1;
                for (int row = 0; row <= 100; ++row) {
                    const auto uv = solar_visual_uv(kind, u, row / 100.0, design);
                    assert(std::isfinite(uv.x) && std::isfinite(uv.y));
                    assert(uv.y > previous); previous = uv.y;
                }
            }
        }
    }
}

static void test_overview_omits_subpixel_surface_detail() {
    auto project = make_solar_system_project();
    project.view_extent = project.bodies[3].display_radius * 8;
    const auto& body = project.bodies[3];
    SceneSnapshot scene; scene.bodies.push_back({body.id, {}, {}, body.mass, body.radius});
    RenderConfig config; config.width = 180; config.height = 160;
    project.bodies[3].design.surface_detail = 0;
    const auto a = render_scene(project, scene, config);
    project.bodies[3].design.surface_detail = 2;
    const auto b = render_scene(project, scene, config);
    assert(a.image.rgb == b.image.rgb);
}

static void test_latitude_detail_tilts_normal_against_increasing_height() {
    // At u=0,v=0.5, increasing v points south, so rising height tilts north.
    const auto latitude = perturb_visual_normal({1, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0, 1);
    const double q = std::sqrt(.5);
    assert(std::abs(latitude.x - q) < 1e-10 && std::abs(latitude.y - q) < 1e-10);
    assert(std::abs(latitude.z) < 1e-10);
    const auto longitude = perturb_visual_normal({1, 0, 0}, {1, 0, 0}, {0, 1, 0}, 1, 0);
    assert(std::abs(longitude.x - q) < 1e-10 && std::abs(longitude.z + q) < 1e-10);
    const auto flat = perturb_visual_normal({1, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0, 0);
    assert(flat.x == 1 && flat.y == 0 && flat.z == 0);
}

int main() {
    test_visual_design_roundtrip_defaults_and_validation();
    test_gas_mapping_preserves_baseline_and_wraps();
    test_moving_storm_moves_relief_and_preserves_sculpt_space();
    test_optical_controls_change_visible_image_without_stealing_picking();
    test_scene_light_follows_sun_and_isolated_light_is_photographic();
    test_atmosphere_clips_against_foreground_and_trace_matches_pixels();
    test_shadow_geometry_and_extreme_storm_mapping();
    test_overview_omits_subpixel_surface_detail();
    test_latitude_detail_tilts_normal_against_increasing_height();
}
