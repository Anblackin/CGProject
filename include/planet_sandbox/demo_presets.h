#pragma once

#include "planet_sandbox/simulation.h"

#include <memory>

namespace planet_sandbox {

enum class DemoPreset { None, EarthTerrain, SaturnRing, Collision };

class DemoPresets {
public:
    DemoPreset active() const { return active_; }
    bool playing() const { return playing_; }
    void activate(DemoPreset preset, const Project& source);
    void clear();
    const Project& project(const Project& source) const;
    SceneSnapshot snapshot(const SceneSnapshot& normal) const;
    void set_playing(bool playing);
    void advance(double real_seconds, double speed = 1.0);
    void step(double simulated_seconds);
    void reset();

private:
    DemoPreset active_ = DemoPreset::None;
    bool playing_ = false;
    Project collision_project_;
    Project collision_physics_;
    std::unique_ptr<Simulation> collision_simulation_;
};

} // namespace planet_sandbox
