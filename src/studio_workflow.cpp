#include "planet_sandbox/studio_ui.h"
#include "planet_sandbox/impact_visual.h"

#include <algorithm>
#include <cmath>

namespace planet_sandbox {

StudioWorkflow studio_workflow(const StudioState& state) {
    const auto& name = state.session.project().bodies[state.selected].name;
    StudioWorkflow result;
    if (state.camera_transition) {
        result.stage = StudioWorkflowStage::Transition;
        const auto destination = state.camera_transition->destination;
        result.title = destination == StudioCameraDestination::Design ? "Opening " + name :
                       destination == StudioCameraDestination::Simulation ? "Starting simulation" :
                       destination == StudioCameraDestination::DesignOverview ? "Returning to design" :
                       "Moving camera";
        result.hint = "Camera in motion. Editing resumes when the view arrives.";
        return result;
    }
    if (state.demos.active() == DemoPreset::Collision) {
        result.stage = StudioWorkflowStage::Collision;
        result.title = "Collision demo";
        result.hint = "Watch Earth and Mars collide. Exit demo to return to your solar system.";
    } else if (state.session.mode() == SessionMode::Simulate) {
        result.stage = StudioWorkflowStage::Simulation;
        result.title = "Simulating / " + name;
        result.hint = state.simulation_follow_id.empty()
            ? "Select a planet for a tracking close-up; select a collision to replay. Return to Design to edit."
            : "Following selected planet. Right drag: rotate. Scroll: zoom. Back to system: overview.";
    } else if (state.design_view) {
        result.stage = StudioWorkflowStage::Design;
        result.title = "Designing / " + name;
        result.hint = "Left drag: sculpt with the brush. Right drag: rotate. Scroll: zoom. Undo: Ctrl/Cmd+Z.";
    } else {
        result.title = (state.session.project().solar_system ? "Solar system / " : "Body / ") + name;
        result.hint = "Click a planet or select its name to open its design. Drag empty space to rotate; scroll to zoom.";
        if (!state.session.project().solar_system) {
            result.hint = "Select a body to edit its appearance or initial orbit. Drag to rotate; scroll to zoom.";
        }
    }
    if (state.compare_modes) {
        result.hint = "Click either image to inspect the same pixel in both. Sculpting is paused during comparison.";
    } else if (state.inspect_pixel) {
        result.hint = "Click to inspect a pixel. Arrow keys: move sample. Right drag: rotate. ";
        result.hint += result.stage == StudioWorkflowStage::Design
            ? "Return to the Design tab and turn off Inspect to sculpt."
            : "Return to Design to edit your planets.";
    } else if (state.pixel_tab_active) {
        result.hint = "The Pixel tool is open. Return to the Design tab to use the brush.";
    } else if (state.compare_baseline && result.stage == StudioWorkflowStage::Design) {
        result.hint = "Viewing the baseline. Uncheck Show baseline to continue editing your design.";
    } else if (!state.design_tab_active && result.stage == StudioWorkflowStage::Design) {
        result.hint = "Adjust the initial orbit and physics here. Return to the Design tab to use the brush.";
    }
    return result;
}

std::vector<StudioBodyMarker> studio_body_markers(const RenderFrame& frame, int body_count) {
    if (body_count <= 0 || frame.image.width <= 0 || frame.image.height <= 0) return {};
    const auto size = static_cast<std::size_t>(frame.image.width) * frame.image.height;
    if (frame.body_index.size() != size || frame.surface_uv.size() != size) return {};
    std::vector<StudioBodyMarker> bounds(static_cast<std::size_t>(body_count));
    for (auto& bound : bounds) {
        bound.min_x = frame.image.width;
        bound.min_y = frame.image.height;
    }
    for (std::size_t pixel = 0; pixel < size; ++pixel) {
        const int body = frame.body_index[pixel];
        // Trails have no surface UV and must not create or enlarge planet markers.
        if (body < 0 || body >= body_count || frame.surface_uv[pixel].x < 0.0 ||
            frame.surface_uv[pixel].y < 0.0) continue;
        auto& bound = bounds[body];
        const int x = static_cast<int>(pixel % frame.image.width);
        const int y = static_cast<int>(pixel / frame.image.width);
        bound.body_index = body;
        bound.min_x = std::min(bound.min_x, x);
        bound.min_y = std::min(bound.min_y, y);
        bound.max_x = std::max(bound.max_x, x);
        bound.max_y = std::max(bound.max_y, y);
    }
    bounds.erase(std::remove_if(bounds.begin(), bounds.end(),
                               [](const auto& bound) { return bound.body_index < 0; }), bounds.end());
    return bounds;
}

bool studio_scene_annotations_visible(const StudioState& state) {
    return !state.compare_modes && !state.inspect_pixel && !state.pixel_tab_active &&
           !state.camera_transition;
}

bool replay_studio_collision(StudioState& state, const CollisionEvent& event) {
    if (state.session.mode() != SessionMode::Simulate || state.camera_transition ||
        state.demos.active() == DemoPreset::Collision || state.compare_modes) return false;
    const CollisionEvent bookmark = event;
    const auto& recorded = state.session.observed_events();
    if (std::none_of(recorded.begin(), recorded.end(), [&](const auto& known) {
            return known.time == bookmark.time && known.survivor == bookmark.survivor &&
                   known.absorbed == bookmark.absorbed;
        })) return false;
    seek_studio_time(state, std::max(0.0, bookmark.time - .5));
    const auto snapshot = state.session.snapshot();
    auto survivor = bookmark.survivor;
    // A second impact in the same replay interval can merge the first survivor again.
    for (const auto& impact : snapshot.events) {
        if (impact.absorbed == survivor) survivor = impact.survivor;
    }
    const auto& bodies = state.session.project().bodies;
    const auto body = std::find_if(bodies.begin(), bodies.end(),
                                   [&](const auto& candidate) { return candidate.id == survivor; });
    if (body != bodies.end()) select_studio_scene_body(state, static_cast<int>(body - bodies.begin()));
    state.simulation_follow_id.clear();
    if(state.camera_transition && bookmark.contact) {
        const auto& c=*bookmark.contact;
        auto& to=state.camera_transition->to;
        const double age=snapshot.time-bookmark.time;
        to.target={c.position.x+c.velocity.x*age,c.position.y+c.velocity.y*age};
        const auto input=make_studio_render_input(state);
        const double merged=std::cbrt(c.survivor_radius*c.survivor_radius*c.survivor_radius+
                                    c.absorbed_radius*c.absorbed_radius*c.absorbed_radius);
        const double radius=merged*(input.project.solar_system && body!=bodies.end()
            ?body->display_radius/body->radius:1.0);
        const double extent=2.5*radius*input.project.display_scale+.5*c.relative_speed;
        to.zoom=std::max(.01,extent/(1.35*input.project.view_extent));
        state.camera_transition->screen_anchor.reset();
        state.camera_transition->resume_playback=false;
    }
    state.collision_replay=StudioState::CollisionReplay{bookmark,
        std::min(100000.0,bookmark.time+impact_visual_lifetime),true};
    state.notice="Collision replay / 0.25x. Visual ejecta do not change the merge physics.";
    return true;
}

void cancel_studio_collision_replay(StudioState& state) {
    if(!state.collision_replay) return;
    state.collision_replay.reset();
    state.session.set_playing(false);
}

bool advance_studio_collision_replay(StudioState& state, double seconds) {
    if(!state.collision_replay) return false;
    if(state.session.mode()!=SessionMode::Simulate) { state.collision_replay.reset();return false; }
    auto& replay=*state.collision_replay;
    if(replay.playing && std::isfinite(seconds) && seconds>0) {
        const double current=state.session.snapshot().time;
        const double target=std::min(replay.end_time,current+std::min(seconds,.1)*.25);
        if(target>current) state.session.step(target-current);
        if(target>=replay.end_time) replay.playing=false;
    }
    if(replay.event.contact) {
        const auto& c=*replay.event.contact;
        const double age=state.session.snapshot().time-replay.event.time;
        state.render.camera.target={c.position.x+c.velocity.x*age,c.position.y+c.velocity.y*age};
    }
    return true;
}

} // namespace planet_sandbox
