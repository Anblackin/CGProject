#include "planet_sandbox/simulation.h"
#include "planet_sandbox/impact_visual.h"
#include "planet_sandbox/renderer.h"
#include "pixel_trace_checks.h"
#include <cassert>
#include <cmath>
#include <limits>
using namespace planet_sandbox;

static void test_contact_is_captured_before_merge_without_changing_physics() {
    Project project;
    project.bodies={
        {"a","A",2,.2,{-0.1,0},{.5,.1},{}},
        {"b","B",1,.1,{.1,0},{-.5,.1},{}}
    };
    Simulation simulation(project);
    const auto snapshot=simulation.snapshot();
    assert(snapshot.events.size()==1 && snapshot.bodies.size()==1);
    const auto& event=snapshot.events.front();
    assert(event.contact);
    const auto& contact=*event.contact;
    assert(event.survivor=="a" && event.absorbed=="b");
    assert(std::abs(contact.normal.x-1)<1e-12 && contact.normal.y==0);
    assert(std::abs(contact.position.x-.1)<1e-12);
    assert(std::abs(contact.relative_speed-1)<1e-12);
    assert(contact.survivor_radius==.2 && contact.absorbed_radius==.1);
    assert(snapshot.bodies.front().mass==3);
    assert(std::abs(snapshot.bodies.front().velocity.x-1.0/6)<1e-12);
    assert(std::abs(snapshot.bodies.front().velocity.y-.1)<1e-12);
    assert(std::abs(snapshot.bodies.front().radius-std::cbrt(.009))<1e-12);
    Simulation again(project);
    assert(again.snapshot().events.front().contact->position.x==contact.position.x);
    project.bodies[1].position=project.bodies[0].position;
    project.bodies[1].velocity=project.bodies[0].velocity;
    Simulation coincident(project);
    const auto stationary=*coincident.snapshot().events.front().contact;
    assert(std::isfinite(stationary.normal.x) && std::isfinite(stationary.normal.y));
    assert(std::abs(std::hypot(stationary.normal.x,stationary.normal.y)-1)<1e-12);
}

static void test_effects_follow_time_and_do_not_replace_picking() {
    Project project;
    project.view_extent=2; project.display_scale=1;
    project.bodies={{"a","A",2,.3,{0,0},{0,0},{}}};
    SceneSnapshot snapshot;
    snapshot.time=1;
    CollisionContact contact;
    contact.normal={1,0}; contact.survivor_radius=.3;
    contact.absorbed_radius=.15; contact.relative_speed=1;
    snapshot.events={{1,"a","b",contact}};
    RenderConfig config;config.width=96;config.height=72;
    const auto sprites=impact_sprites(project,snapshot);
    assert(!sprites.empty() && sprites.size()<=34);
    const auto frame=render_scene(project,snapshot,config);
    config.impact_effects=false;
    const auto baseline=render_scene(project,snapshot,config);
    assert(frame.image.rgb!=baseline.image.rgb);
    assert(frame.body_index==baseline.body_index);
    assert(frame.surface_uv.size()==baseline.surface_uv.size());
    for(std::size_t i=0;i<frame.surface_uv.size();++i) {
        assert(frame.surface_uv[i].x==baseline.surface_uv[i].x && frame.surface_uv[i].y==baseline.surface_uv[i].y);
        assert(frame.sample_surface_variation[i]==baseline.sample_surface_variation[i]);
    }
    config.impact_effects=true;
    assert(render_scene(project,snapshot,config).image.rgb==frame.image.rgb);
    snapshot.time=.99; assert(impact_sprites(project,snapshot).empty());
    assert(render_scene(project,snapshot,config).image.rgb==baseline.image.rgb);
    snapshot.time=2.51; assert(impact_sprites(project,snapshot).empty());
    assert(render_scene(project,snapshot,config).image.rgb==baseline.image.rgb);
    snapshot.time=1.1;
    assert(render_scene(project,snapshot,config).image.rgb!=frame.image.rgb);
    for(const auto mode:{SamplingMode::Single,SamplingMode::MSAA4,SamplingMode::SSAA4}) {
        snapshot.time=1;
        config.sampling=mode;
        config.impact_effects=false;
        const auto off=render_scene(project,snapshot,config);
        config.impact_effects=true;
        const auto on=render_scene(project,snapshot,config);
        std::size_t pixel=0;
        while(pixel<on.body_index.size() &&
            on.image.rgb[pixel*3]==off.image.rgb[pixel*3] &&
            on.image.rgb[pixel*3+1]==off.image.rgb[pixel*3+1] &&
            on.image.rgb[pixel*3+2]==off.image.rgb[pixel*3+2]) ++pixel;
        assert(pixel<on.body_index.size());
        config.trace_x=static_cast<int>(pixel%config.width);
        config.trace_y=static_cast<int>(pixel/config.width);
        const auto traced=render_scene(project,snapshot,config);
        assert(traced.trace && traced.trace->process_recorded);
        check_pixel_process(*traced.trace);
        assert(std::any_of(traced.trace->events.begin(),traced.trace->events.end(),
            [](const auto& event){return event.primitive==TracePrimitive::Impact;}));
        config.trace_x=config.trace_y=-1;
    }
    // A different foreground body occludes the effect, rather than resizing its survivor.
    project.bodies.push_back({"foreground","Foreground",2,1,{0,0},{0,0},{}});
    snapshot.bodies={{"foreground",{0,0},{0,0},2,1}};
    config.impact_effects=false;
    const auto occluder=render_scene(project,snapshot,config);
    config.impact_effects=true;
    assert(render_scene(project,snapshot,config).image.rgb==occluder.image.rgb);
    snapshot.events[0].contact.reset();
    assert(impact_sprites(project,snapshot).empty());
}

static void test_large_absorbed_body_does_not_hide_impact_inside_survivor() {
    Project project;
    project.view_extent=2;project.display_scale=1;
    project.bodies={{"a","A",2,.1,{0,0},{0,0},{}},
                    {"b","B",1,1,{.9,0},{0,0},{}}};
    Simulation simulation(project);
    auto snapshot=simulation.snapshot();
    assert(snapshot.bodies.size()==1 && snapshot.events.front().contact);
    RenderConfig config;config.width=96;config.height=72;
    for(double age:{0.0,.1,.5,1.0}) {
        snapshot.time=age;
        config.impact_effects=false;
        const auto off=render_scene(project,snapshot,config);
        config.impact_effects=true;
        const auto on=render_scene(project,snapshot,config);
        assert(on.image.rgb!=off.image.rgb);
        assert(on.body_index==off.body_index);
    }
}

static void test_effect_generation_has_a_fixed_event_budget() {
    Project project;
    project.bodies={{"a","A",2,.3,{0,0},{0,0},{}}};
    SceneSnapshot snapshot;snapshot.time=1;
    CollisionContact contact;contact.normal={1,0};
    contact.survivor_radius=.3;contact.absorbed_radius=.1;
    for(int i=0;i<20;++i) snapshot.events.push_back({1,"a",std::to_string(i),contact});
    assert(impact_sprites(project,snapshot).size()==104);
    snapshot.time=3;
    assert(impact_sprites(project,snapshot).empty());
}

static void test_surface_patch_cools_fades_and_does_not_modify_geometry() {
    Project project;
    project.view_extent=1;project.display_scale=1;
    project.bodies={{"a","A",2,.3,{0,0},{0,0},{}}};
    project.bodies[0].appearance.seed=0;
    SceneSnapshot snapshot;
    snapshot.bodies={{"a",{0,0},{0,0},2,.3}};
    CollisionContact contact;
    contact.normal={1,0};contact.survivor_radius=.3;contact.absorbed_radius=.15;
    snapshot.events={{0,"a","b",contact}};
    snapshot.time=2;
    assert(impact_sprites(project,snapshot).empty());
    const auto patches=impact_surface_patches(project.bodies[0],snapshot);
    assert(patches.size()==1);
    const auto warm=sample_impact_surface(patches,0,.5);
    const auto opposite=sample_impact_surface(patches,.5,.5);
    assert(warm.emission[0]>0 && warm.emission[0]>warm.emission[2]);
    assert(opposite.emission[0]==0 && opposite.darkening==0);
    snapshot.time=4;
    const auto cool=sample_impact_surface(impact_surface_patches(project.bodies[0],snapshot),0,.5);
    assert(cool.emission[0]<warm.emission[0]);
    RenderConfig config;config.width=128;config.height=96;config.camera.zoom=.6;
    snapshot.time=2;
    config.camera.yaw=1.5707963267948966+snapshot.time*.45;
    config.impact_effects=false;
    const auto off=render_scene(project,snapshot,config);
    config.impact_effects=true;
    const auto on=render_scene(project,snapshot,config);
    assert(on.image.rgb!=off.image.rgb && on.body_index==off.body_index);
    assert(on.sample_surface_variation==off.sample_surface_variation);
    for(std::size_t i=0;i<on.surface_uv.size();++i) {
        assert(on.surface_uv[i].x==off.surface_uv[i].x && on.surface_uv[i].y==off.surface_uv[i].y);
    }
    assert(render_scene(project,snapshot,config).image.rgb==on.image.rgb);
    for(const auto mode:{SamplingMode::Single,SamplingMode::MSAA4,SamplingMode::SSAA4}) {
        config.sampling=mode;
        config.trace_x=64;config.trace_y=48;
        const auto trace_frame=render_scene(project,snapshot,config);
        assert(trace_frame.trace);
        check_pixel_process(*trace_frame.trace);
        config.trace_x=config.trace_y=-1;
        const auto compare=render_comparison_scene(project,snapshot,config,mode,mode);
        const auto single=render_scene(project,snapshot,config);
        for(int y=0;y<config.height;++y) for(int x=0;x<config.width;++x) for(int channel=0;channel<3;++channel) {
            const auto source=(std::size_t(y)*config.width+x)*3+channel;
            const auto left=(std::size_t(y)*config.width*2+x)*3+channel;
            const auto right=left+config.width*3;
            assert(compare.image.rgb[left]==single.image.rgb[source]);
            assert(compare.image.rgb[right]==single.image.rgb[source]);
        }
    }
    snapshot.time=6.01;
    assert(impact_surface_patches(project.bodies[0],snapshot).empty());
    config.impact_effects=false;const auto expired=render_scene(project,snapshot,config);
    config.impact_effects=true;
    assert(render_scene(project,snapshot,config).image.rgb==expired.image.rgb);
    snapshot.time=-.01;
    assert(impact_surface_patches(project.bodies[0],snapshot).empty());
    snapshot.time=2;snapshot.events[0].contact.reset();
    assert(impact_surface_patches(project.bodies[0],snapshot).empty());
}

static void test_surface_patch_uses_impact_time_spin_and_axial_orientation() {
    BodySpec body;body.id="a";body.kind=BodyKind::Earth;
    CollisionContact contact;contact.normal={1,0};
    contact.survivor_radius=.3;contact.absorbed_radius=.15;
    SceneSnapshot snapshot;snapshot.time=3;
    snapshot.events={{2,"a","b",contact}};
    auto patches=impact_surface_patches(body,snapshot);
    // At t=2, a +X impact corresponds to local longitude +0.9 radians.
    constexpr double pi=3.14159265358979323846;
    assert(sample_impact_surface(patches,.9/(2*pi),.5).emission[0]>0);
    assert(sample_impact_surface(patches,0,.5).emission[0]==0);
    const auto initial=patches.front().local_direction;
    snapshot.time=4;
    patches=impact_surface_patches(body,snapshot);
    assert(patches.front().local_direction.x==initial.x && patches.front().local_direction.z==initial.z);
    const auto rotation=body_surface_rotation(BodyKind::Earth,0,0,pi/(2*.45));
    const auto rotated=rotation.to_world({1,0,0});
    assert(std::abs(rotated.x)<1e-12 && std::abs(rotated.z+1)<1e-12);
    const auto inverse=rotation.to_local(rotated);
    assert(std::abs(inverse.x-1)<1e-12 && std::abs(inverse.z)<1e-12);

    body.kind=BodyKind::Uranus;body.design.axis_tilt_offset=-8;
    snapshot.time=1;snapshot.events[0].time=0;
    snapshot.events[0].contact->normal={0,1};
    patches=impact_surface_patches(body,snapshot);
    // A 90-degree axial tilt maps local +X to world +Y.
    assert(sample_impact_surface(patches,0,.5).emission[0]>0);
    assert(sample_impact_surface(patches,.5,.5).emission[0]==0);
    body.design.axis_tilt_offset=-98;
    snapshot.events[0].contact->normal={1,0};
    patches=impact_surface_patches(body,snapshot);
    assert(sample_impact_surface(patches,.35/(2*pi),.5).emission[0]>0);
    body.kind=BodyKind::Custom;body.appearance.seed=100;
    patches=impact_surface_patches(body,snapshot);
    assert(sample_impact_surface(patches,1.7/(2*pi),.5).emission[0]>0);
    body.id="unrelated";
    assert(impact_surface_patches(body,snapshot).empty());
}

static void test_surface_patches_are_bounded_seamless_and_ignore_invalid_contacts() {
    BodySpec body;body.id="a";body.appearance.seed=0;
    CollisionContact contact;contact.normal={1,0};contact.survivor_radius=.3;contact.absorbed_radius=.2;
    SceneSnapshot snapshot;snapshot.time=2;
    for(int i=0;i<20;++i) snapshot.events.push_back({0,"a",std::to_string(i),contact});
    auto patches=impact_surface_patches(body,snapshot);
    assert(patches.size()==4);
    const auto left=sample_impact_surface(patches,0,.5),right=sample_impact_surface(patches,1,.5);
    for(int i=0;i<3;++i) assert(std::abs(left.emission[i]-right.emission[i])<1e-12);
    assert(std::abs(left.darkening-right.darkening)<1e-12);
    assert(left.darkening<=.65);
    for(auto& event:snapshot.events) event.contact->normal={0,0};
    assert(impact_surface_patches(body,snapshot).empty());
    snapshot.events[0].contact->normal={std::numeric_limits<double>::quiet_NaN(),0};
    assert(impact_surface_patches(body,snapshot).empty());
    snapshot.events[0].contact->normal={1,0};snapshot.events[0].contact->survivor_radius=0;
    assert(impact_surface_patches(body,snapshot).empty());
}

static void test_sandbox_patch_uses_the_same_rotation_as_its_rendered_mesh() {
    Project project;project.view_extent=1;project.display_scale=1;
    project.bodies={{"a","A",2,.3,{0,0},{0,0},{}}};
    project.bodies[0].kind=BodyKind::Earth;
    project.bodies[0].appearance.seed=100;
    SceneSnapshot snapshot;snapshot.time=2;
    snapshot.bodies={{"a",{0,0},{0,0},2,.3}};
    CollisionContact contact;contact.normal={1,0};contact.survivor_radius=.3;contact.absorbed_radius=.15;
    snapshot.events={{0,"a","b",contact}};
    RenderConfig config;config.width=128;config.height=96;config.camera.zoom=.6;
    config.camera.yaw=1.5707963267948966+.9;
    config.impact_effects=false;
    const auto off=render_scene(project,snapshot,config);
    config.impact_effects=true;
    const auto on=render_scene(project,snapshot,config);
    const std::size_t red=(48*128+64)*3;
    assert(on.image.rgb[red]>off.image.rgb[red]);
}

int main() {
    test_contact_is_captured_before_merge_without_changing_physics();
    test_effects_follow_time_and_do_not_replace_picking();
    test_large_absorbed_body_does_not_hide_impact_inside_survivor();
    test_effect_generation_has_a_fixed_event_budget();
    test_surface_patch_cools_fades_and_does_not_modify_geometry();
    test_surface_patch_uses_impact_time_spin_and_axial_orientation();
    test_surface_patches_are_bounded_seamless_and_ignore_invalid_contacts();
    test_sandbox_patch_uses_the_same_rotation_as_its_rendered_mesh();
}
