#pragma once

#include "planet_sandbox/project.h"

#include <memory>
#include <string>
#include <vector>

namespace planet_sandbox {

struct BodyState {
    std::string id;
    Vec2 position;
    Vec2 velocity;
    double mass = 0.0;
    double radius = 0.0;
};

struct CollisionEvent {
    double time = 0.0;
    std::string survivor;
    std::string absorbed;
};

struct BodyTrail {
    std::string id;
    std::vector<Vec2> points;
};

struct SceneSnapshot {
    double time = 0.0;
    std::vector<BodyState> bodies;
    std::vector<CollisionEvent> events;
    std::vector<BodyTrail> trails;
    std::vector<BodyTrail> reference_trails;
};

class Simulation {
public:
    explicit Simulation(const Project& project);
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    void advance_to(double time);
    SceneSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace planet_sandbox
