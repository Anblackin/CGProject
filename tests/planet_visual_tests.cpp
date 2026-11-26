#include "planet_sandbox/planet_visual.h"
#include "planet_sandbox/solar_system.h"
#include "planet_sandbox/texture.h"
#include "planet_sandbox/sculpt.h"
#include "planet_sandbox/renderer.h"
#include "pixel_trace_checks.h"
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

static void test_selective_texture_samples_match_rgba_exactly() {
    const Texture gray{3,2,{0,0,0,1, 127,127,127,255, 255,255,255,81,
                            83,83,83,91, 19,19,19,12, 213,213,213,250}};
    auto mixed=gray;mixed.rgba[5]=64;
    const Texture colored{2,2,{12,250,0,31, 17,65,121,80, 251,95,7,98, 0,45,31,255}};
    const Texture single{1,1,{17,83,171,42}};
    for(const auto* texture:std::array<const Texture*,4>{&gray,&mixed,&colored,&single}) {
        for(bool wrap:{false,true}) for(double u:{-3.1,-1.0,-.0001,0.0,.25,.5,.9999,1.0,1.001,4.3})
            for(double v:{-1.0,0.0,.0001,.25,.75,.9999,1.0,2.0}) {
                const auto rgba=sample_texture(*texture,u,v,wrap);
                for(int c=0;c<4;++c) assert(sample_texture_channel(*texture,u,v,c,wrap)==rgba[c]);
                assert(sample_texture_rgb_mean(*texture,u,v,wrap)==(rgba[0]+rgba[1]+rgba[2])/3);
            }
    }
    // Public texture data may change; scalar sampling cannot retain stale grayscale metadata.
    auto updated=gray;
    for(int c=0;c<3;++c) {
        updated.rgba[c]=c*67;
        const auto rgba=sample_texture(updated,.12,.41);
        assert(sample_texture_rgb_mean(updated,.12,.41)==(rgba[0]+rgba[1]+rgba[2])/3);
    }
    const auto& asset=solar_texture(BodyKind::Earth,AssetLayer::Clouds);
    for(int p=0;p<1000;++p) {
        const double u=(p%97)/96.0,v=(p%89)/88.0;
        const auto rgba=sample_texture(asset,u,v);
        assert(sample_texture_rgb_mean(asset,u,v)==(rgba[0]+rgba[1]+rgba[2])/3);
    }
}

static void test_selective_texture_samples_reject_invalid_inputs() {
    const Texture valid{1,1,{10,20,30,40}};
    for(int channel:{-1,4,99}) {
        bool rejected=false;
        try {sample_texture_channel(valid,0,0,channel);} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);
    }
    for(const Texture invalid:{Texture{},Texture{1,1,{1,2,3}},Texture{-1,1,{}}}) {
        bool channel=false,mean=false;
        try{sample_texture_channel(invalid,0,0,0);}catch(const std::invalid_argument&){channel=true;}
        try{sample_texture_rgb_mean(invalid,0,0);}catch(const std::invalid_argument&){mean=true;}
        assert(channel && mean);
    }
    for(double bad:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
        for(bool first:{false,true}) {
            const double u=first?bad:0,v=first?0:bad;
            bool channel=false,mean=false;
            try{sample_texture_channel(valid,u,v,0);}catch(const std::invalid_argument&){channel=true;}
            try{sample_texture_rgb_mean(valid,u,v);}catch(const std::invalid_argument&){mean=true;}
            assert(channel && mean);
        }
}

static void test_ring_optics_are_bounded_and_distinguish_the_transmitted_face() {
    assert(ring_light_transmission(0,.2)==1);
    assert(ring_light_transmission(1,.2)==0);
    const double opacity=1-std::exp(-1.0);
    assert(std::abs(ring_light_transmission(opacity,.5)-std::exp(-2))<1e-12);
    assert(ring_light_transmission(.5,.1)<ring_light_transmission(.5,.8));
    assert(ring_light_transmission(.5,-.5)==ring_light_transmission(.5,.5));
    assert(ring_reflectance(.9,.5,.5)>ring_reflectance(.9,.5,-.5));
    assert(ring_reflectance(.1,.5,-.5)>ring_reflectance(.9,.5,-.5));
    for(double alpha:{0.0,1e-9,.1,.8,1.0}) for(double nl:{-1.0,-.5,-1e-9,0.0,.5,1.0})
        for(double nv:{-1.0,-.5,-1e-9,0.0,.5,1.0}) {
            const double brightness=ring_reflectance(alpha,nl,nv);
            assert(std::isfinite(brightness) && brightness>=0 && brightness<=1);
            assert(brightness==ring_reflectance(alpha,-nl,-nv));
            if(alpha==0) assert(brightness==0);
            else {
                assert(ring_reflectance(alpha,nl,nv,0)==.025);
                if(nl==0) assert(brightness==.025);
            }
        }
    const double tau=-std::log1p(-.8);
    const double expected=.025+.975*std::min(1.0,1.6*(tau/.5*std::exp(-tau/.5))/.8);
    assert(std::abs(ring_reflectance(.8,.5,-.5)-expected)<1e-12);
    for(double offset:{-1e-7,-1e-9,0.0,1e-9,1e-7})
        assert(std::abs(ring_reflectance(.8,.5,-(.5+offset))-expected)<1e-6);
}

static void test_saturn_ring_traces_replay_on_both_faces() {
    auto project=make_solar_system_project();
    const auto& saturn=project.bodies[6];project.view_extent=saturn.display_radius*2;
    SceneSnapshot scene;scene.bodies={{saturn.id,{},{},saturn.mass,saturn.radius}};
    for(double pitch:{-.6,.8}) for(auto sampling:{SamplingMode::Single,SamplingMode::MSAA4,SamplingMode::SSAA4}) {
        RenderConfig config;config.width=140;config.height=120;config.camera.pitch=pitch;config.sampling=sampling;
        const auto plain=render_scene(project,scene,config);
        std::size_t pixel=plain.body_index.size();
        for(std::size_t p=0;p<plain.body_index.size();++p)
            if(plain.body_index[p]==6 && plain.surface_uv[p].x<0) {pixel=p;break;}
        assert(pixel<plain.body_index.size());
        config.trace_x=int(pixel%config.width);config.trace_y=int(pixel/config.width);
        const auto traced=render_scene(project,scene,config);
        assert(traced.image.rgb==plain.image.rgb && traced.body_index==plain.body_index);
        check_pixel_process(*traced.trace);
        bool written_ring=false;
        for(const auto& event:traced.trace->events) if(event.surface==TraceSurface::Ring)
            for(const auto& sample:event.samples) written_ring|=sample.action==TraceAction::Written;
        assert(written_ring);
    }
}

static void test_cloud_optics_preserve_coverage_and_bound_daylight() {
    assert(cloud_light_transmission(0,1,.1)==1);
    assert(cloud_light_transmission(1,0,.1)==1);
    assert(std::abs(cloud_light_transmission(1,1,1)-std::exp(-2))<1e-12);
    assert(cloud_light_transmission(1,1,.1)<cloud_light_transmission(1,1,1));
    double previous=1;
    for(double mask:{0.0,.01,.2,.7,1.0}) {
        const double transmitted=cloud_light_transmission(mask,.75,.5);
        assert(transmitted<=previous && transmitted>=0 && transmitted<=1);
        previous=transmitted;
        for(double opacity:{0.0,.35,1.0}) for(double sun:{-1.0,-.1,0.0,.04,.8,1.0})
            for(double view:{0.0,.001,.35,1.0}) for(double phase:{-1.0,0.0,1.0}) {
                const auto cloud=cloud_reflectance(mask,opacity,sun,1,view,phase);
                const double expected=1-std::exp(-opacity*mask*2/std::max(.35,view));
                assert(cloud.alpha==expected);
                assert(std::isfinite(cloud.brightness) && cloud.brightness>=0 && cloud.brightness<=1);
                if(mask==0 || opacity==0) assert(cloud.alpha==0 && cloud.brightness==0);
                else if(sun<=-.02) assert(cloud.brightness==.025);
            }
    }
    const auto thin=cloud_reflectance(.1,1,1,1,1,1);
    const auto thick=cloud_reflectance(1,1,1,1,1,1);
    assert(thick.alpha>thin.alpha && thick.brightness>thin.brightness);
    const auto front=cloud_reflectance(.8,1,.1,.1,.1,1);
    const auto backlit=cloud_reflectance(.8,1,.1,.1,.1,-1);
    assert(backlit.brightness>front.brightness);
}

static void test_cloud_depth_changes_lighting_and_shadows_in_the_renderer() {
    auto project=make_solar_system_project();project.view_extent=project.bodies[3].display_radius*1.08;
    const auto& earth=project.bodies[3];
    SceneSnapshot scene;scene.bodies={{earth.id,{},{},earth.mass,earth.radius}};
    RenderConfig config;config.width=120;config.height=100;config.sampling=SamplingMode::MSAA4;
    config.camera.yaw=.25;config.camera.pitch=.12;
    project.bodies[3].design.cloud_shadow=1;
    bool lit_cloud=false,cloud_brightness_changes=false,shadow_curvature=false;
    for(int y:{35,50,65}) for(int x:{40,60,80}) {
        config.trace_x=x;config.trace_y=y;
        std::array<RenderFrame,3> frames;
        for(int i=0;i<3;++i) {
            project.bodies[3].design.cloud_opacity=.25*(i+1);
            frames[i]=render_scene(project,scene,config);
            check_pixel_process(*frames[i].trace);
        }
        const auto& first=frames[0].trace->events;
        for(const auto& event:first) {
            if(event.body_index!=3 || event.shading.empty()) continue;
            std::array<const TraceEvent*,3> matching{&event,nullptr,nullptr};
            for(int i=1;i<3;++i) for(const auto& candidate:frames[i].trace->events)
                if(candidate.body_index==3 && candidate.primitive==event.primitive &&
                   candidate.primitive_id==event.primitive_id && candidate.clipped_part==event.clipped_part &&
                   candidate.surface==event.surface && !candidate.shading.empty()) matching[i]=&candidate;
            if(!matching[1] || !matching[2]) continue;
            const auto& a=matching[0]->shading[0];
            const auto& b=matching[1]->shading[0];
            const auto& c=matching[2]->shading[0];
            if(event.surface==TraceSurface::Cloud && a.alpha>.01 && a.linear_rgb[0]>.04) {
                lit_cloud=true;
                cloud_brightness_changes|=a.linear_rgb!=c.linear_rgb;
            }
            if(event.surface==TraceSurface::Body)
                for(int channel=0;channel<3;++channel)
                    shadow_curvature|=a.linear_rgb[channel]+c.linear_rgb[channel]-2*b.linear_rgb[channel]>1e-7;
        }
    }
    assert(lit_cloud);
    assert(cloud_brightness_changes);
    assert(shadow_curvature);
}

static void test_cloud_drift_is_repeatable_and_disappears_with_clear_skies() {
    auto project=make_solar_system_project();project.view_extent=project.bodies[3].display_radius*1.08;
    const auto& earth=project.bodies[3];
    SceneSnapshot scene;scene.time=8;
    scene.bodies={{earth.id,{},{},earth.mass,earth.radius}};
    RenderConfig config;config.width=120;config.height=100;config.sampling=SamplingMode::MSAA4;
    const auto drifting=render_scene(project,scene,config);
    assert(render_scene(project,scene,config).image.rgb==drifting.image.rgb);
    project.bodies[3].design.cloud_motion=0;
    assert(render_scene(project,scene,config).image.rgb!=drifting.image.rgb);
    project.bodies[3].design.cloud_opacity=0;
    const auto clear=render_scene(project,scene,config);
    project.bodies[3].design.cloud_motion=1;
    assert(render_scene(project,scene,config).image.rgb==clear.image.rgb);
}

static void test_surface_roughness_roundtrip_and_validation() {
    auto project=make_solar_system_project();
    project.bodies[1].design.surface_roughness=.13;
    project.bodies[3].design.surface_roughness=.92;
    std::ostringstream output;write_project(output,project);
    std::istringstream input(output.str());
    const auto restored=read_project(input);
    assert(restored.bodies[1].design.surface_roughness==.13);
    assert(restored.bodies[3].design.surface_roughness==.92);
    auto legacy=nlohmann::json::parse(output.str());
    for(auto& body:legacy["bodies"]) body["overrides"].erase("surface_roughness");
    std::istringstream old_input(legacy.dump());
    assert(read_project(old_input).bodies[1].design.surface_roughness==.7);
    for(double invalid:{-.1,1.1,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        project.bodies[1].design.surface_roughness=invalid;
        bool rejected=false;
        try{validate_project(project);}catch(const std::invalid_argument&){rejected=true;}
        assert(rejected);
    }
}

static void test_rock_response_is_rough_bounded_and_dark_without_sunlight() {
    const VisualVector n{0,0,1},l{.6,0,.8},v{0,0,1};
    const auto smooth=rock_reflectance(n,l,v,0);
    assert(std::abs(smooth.diffuse-.8)<1e-12);
    const auto rough=rock_reflectance(n,l,v,1);
    assert(rough.diffuse<smooth.diffuse && rough.diffuse>0);
    assert(rock_reflectance(n,n,n,0).specular>rock_reflectance(n,n,n,1).specular);
    const auto dark=rock_reflectance(n,{0,0,-1},v,.7);
    assert(dark.diffuse==0 && dark.specular==0);
    for(double r:{0.0,.01,.7,1.0}) for(double z:{0.0,.000001,.1,.9,1.0}) {
        const auto reflected=rock_reflectance(n,{std::sqrt(1-z*z),0,z},v,r);
        assert(std::isfinite(reflected.diffuse) && std::isfinite(reflected.specular));
        assert(reflected.diffuse>=0 && reflected.diffuse<=1);
        assert(reflected.specular>=0 && reflected.specular<=.12);
    }
}

static void test_ocean_and_added_ice_are_independent_of_land_roughness() {
    const VisualVector normal{0,0,1},light{.6,0,.8},view{0,0,1};
    const auto ice=rock_reflectance(normal,light,view,.18);
    for(double roughness:{0.0,.3,.7,1.0}) {
        // Controlled mask endpoints do not depend on blue pixels in a specific asset or view.
        const auto ocean=rock_surface_reflectance(normal,light,view,roughness,1,0);
        assert(std::abs(ocean.diffuse-.8)<1e-12 && ocean.specular==0);
        const auto frozen=rock_surface_reflectance(normal,light,view,roughness,0,1);
        assert(std::abs(frozen.diffuse-ice.diffuse)<1e-12);
        assert(std::abs(frozen.specular-2*ice.specular)<1e-12);
        const auto land=rock_surface_reflectance(normal,light,view,roughness,0,0);
        const auto rock=rock_reflectance(normal,light,view,roughness);
        assert(land.diffuse==rock.diffuse && land.specular==rock.specular);
        for(double coverage:{.01,.5,.99}) {
            const auto coast=rock_surface_reflectance(normal,light,view,roughness,coverage,0);
            assert(std::abs(coast.diffuse-(rock.diffuse*(1-coverage)+.8*coverage))<1e-12);
            assert(std::abs(coast.specular-rock.specular*(1-coverage))<1e-12);
        }
    }
}

static void test_rock_roughness_changes_shading_without_changing_geometry() {
    for(int index:{1,2,3,4}) {
        auto smooth=make_solar_system_project();
        smooth.bodies[index].design.surface_roughness=0;
        smooth.bodies[index].design.cloud_opacity=0;
        auto rough=smooth;rough.bodies[index].design.surface_roughness=1;
        const auto a=closeup(smooth,index),b=closeup(rough,index);
        assert(a.image.rgb!=b.image.rgb);
        assert(a.body_index==b.body_index && a.sample_surface_variation==b.sample_surface_variation);
        for(std::size_t i=0;i<a.surface_uv.size();++i) {
            assert(a.surface_uv[i].x==b.surface_uv[i].x && a.surface_uv[i].y==b.surface_uv[i].y);
        }
    }
    for(int index:{0,5,6,7,8}) {
        auto smooth=make_solar_system_project();smooth.bodies[index].design.surface_roughness=0;
        auto rough=smooth;rough.bodies[index].design.surface_roughness=1;
        assert(closeup(smooth,index).image.rgb==closeup(rough,index).image.rgb);
    }
    auto venus=make_solar_system_project();venus.bodies[2].design.cloud_opacity=1;
    venus.bodies[2].design.surface_roughness=0;
    const auto cloud=closeup(venus,2);
    venus.bodies[2].design.surface_roughness=1;
    assert(closeup(venus,2).image.rgb==cloud.image.rgb);
}

int main() {
    test_selective_texture_samples_match_rgba_exactly();
    test_selective_texture_samples_reject_invalid_inputs();
    test_ring_optics_are_bounded_and_distinguish_the_transmitted_face();
    test_saturn_ring_traces_replay_on_both_faces();
    test_cloud_optics_preserve_coverage_and_bound_daylight();
    test_cloud_depth_changes_lighting_and_shadows_in_the_renderer();
    test_cloud_drift_is_repeatable_and_disappears_with_clear_skies();
    test_surface_roughness_roundtrip_and_validation();
    test_rock_response_is_rough_bounded_and_dark_without_sunlight();
    test_rock_roughness_changes_shading_without_changing_geometry();
    test_ocean_and_added_ice_are_independent_of_land_roughness();
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
