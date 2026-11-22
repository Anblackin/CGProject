#pragma once

#include "planet_sandbox/project.h"
#include "planet_sandbox/simulation.h"

#include <memory>
#include <optional>
#include <vector>

namespace planet_sandbox {

enum class SessionMode { Design, Simulate };

class SandboxSession {
public:
    explicit SandboxSession(Project project);

    const Project& project() const { return project_; }
    std::uint64_t project_revision() const { return project_revision_; }
    SessionMode mode() const { return mode_; }
    bool playing() const { return playing_; }
    double speed() const { return speed_; }
    SceneSnapshot snapshot() const { return simulation_->snapshot(); }
    std::optional<SceneSnapshot> reference_snapshot() const;
    const std::vector<CollisionEvent>& observed_events() const { return observed_events_; }

    void replace_project(Project project);
    void add_planet();
    void reset_body_design(BodyKind kind);
    void begin_simulation();
    void return_to_design();
    void reset_simulation();
    void set_playing(bool playing);
    void set_speed(double speed);
    void advance(double real_seconds);
    void step(double simulated_seconds);
    void seek(double simulated_time);

private:
    Project project_;
    std::uint64_t project_revision_ = 0;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<Simulation> reference_simulation_;
    std::vector<CollisionEvent> observed_events_;
    SessionMode mode_ = SessionMode::Design;
    bool playing_ = false;
    double speed_ = 1.0;
    void record_events();
};

} // namespace planet_sandbox
