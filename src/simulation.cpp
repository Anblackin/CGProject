#include "planet_sandbox/simulation.h"
#include "planet_sandbox/solar_system.h"

extern "C" {
#if defined(_MSC_VER)
#define restrict __restrict
#else
#define restrict __restrict__
#endif
#include <rebound.h>
#include <simulation.h>
#undef restrict
}

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace planet_sandbox {

struct Simulation::Impl {
    reb_simulation* simulation = nullptr;
    std::vector<CollisionEvent> events;
    std::vector<BodyTrail> trails;
    std::vector<std::string> identity_names;

    ~Impl() { reb_simulation_free(simulation); }

    static REB_COLLISION_RESOLVE_OUTCOME merge_collision(reb_simulation* simulation, reb_collision collision) {
        auto* self = static_cast<Impl*>(simulation->extras);
        const auto first = collision.p1 < collision.p2 ? collision.p1 : collision.p2;
        const auto second = collision.p1 < collision.p2 ? collision.p2 : collision.p1;
        const auto& a = simulation->particles[first];
        const auto& b = simulation->particles[second];
        const std::string a_id = a.name ? a.name : "";
        const std::string b_id = b.name ? b.name : "";
        const auto catalog_rank = [](const std::string& id) {
            for (std::size_t i = 0; i < 9; ++i) {
                if (id == body_kind_id(solar_body_kind(i))) return static_cast<int>(i);
            }
            return 100;
        };
        const bool second_wins = b.m > a.m ||
            (b.m == a.m && (catalog_rank(b_id) < catalog_rank(a_id) ||
                            (catalog_rank(b_id) == catalog_rank(a_id) && b_id < a_id)));
        const std::string survivor = second_wins ? b_id : a_id;
        const std::string absorbed = second_wins ? a_id : b_id;
        const auto& target=second_wins?b:a;
        const auto& incoming=second_wins?a:b;
        const double dx=incoming.x-target.x, dy=incoming.y-target.y;
        const double distance=std::hypot(dx,dy);
        const double vx=incoming.vx-target.vx, vy=incoming.vy-target.vy;
        const double speed=std::hypot(vx,vy);
        const Vec2 normal=distance>1e-12 ? Vec2{dx/distance,dy/distance}
            : speed>1e-12 ? Vec2{-vx/speed,-vy/speed} : Vec2{1,0};
        const double total_mass=a.m+b.m;
        CollisionContact contact;
        contact.normal=normal;
        contact.survivor_position={target.x,target.y};
        contact.position={target.x+normal.x*target.r,target.y+normal.y*target.r};
        contact.velocity={(a.m*a.vx+b.m*b.vx)/total_mass,(a.m*a.vy+b.m*b.vy)/total_mass};
        contact.relative_speed=speed;
        contact.survivor_radius=target.r; contact.absorbed_radius=incoming.r;
        self->events.push_back({simulation->t, survivor, absorbed, contact});
        const auto outcome = reb_collision_resolve_merge(simulation, collision);
        if (second_wins) {
            const auto found = std::find(self->identity_names.begin(), self->identity_names.end(), survivor);
            if (found != self->identity_names.end()) reb_particle_set_name(&simulation->particles[first],found->c_str());
        }
        return outcome;
    }

    void record_positions() {
        for (std::size_t i = 0; i < simulation->N; ++i) {
            const auto& particle = simulation->particles[i];
            const std::string id = particle.name ? particle.name : "";
            auto found = std::find_if(trails.begin(), trails.end(), [&](const BodyTrail& trail) { return trail.id == id; });
            if (found == trails.end()) {
                trails.push_back({id, {}});
                found = trails.end() - 1;
            }
            found->points.push_back({particle.x, particle.y});
            if (found->points.size() > 1024) found->points.erase(found->points.begin());
        }
    }
};

Simulation::Simulation(const Project& project) : impl_(new Impl) {
    validate_project(project);
    impl_->simulation = reb_simulation_create();
    if (!impl_->simulation) throw std::runtime_error("Could not initialize REBOUND");
    auto* simulation = impl_->simulation;
    simulation->extras = impl_.get();
    simulation->G = 1.0;
    simulation->dt = 0.001;
    reb_simulation_set_integrator(simulation, "ias15");
    simulation->collision = reb_simulation::REB_COLLISION_LINE;
    simulation->collision_resolve = &Impl::merge_collision;
    impl_->identity_names.reserve(project.bodies.size());
    for (const auto& body : project.bodies) impl_->identity_names.push_back(body.id);
    for (const auto& body : project.bodies) {
        reb_particle particle{};
        particle.x = body.position.x;
        particle.y = body.position.y;
        particle.vx = body.velocity.x;
        particle.vy = body.velocity.y;
        particle.m = body.mass;
        particle.r = body.radius;
        const std::size_t index = static_cast<std::size_t>(&body - project.bodies.data());
        particle.name = impl_->identity_names[index].c_str();
        reb_simulation_add(simulation, particle);
    }
    bool merged = true;
    while (merged) {
        merged = false;
        for (std::size_t i = 0; i < simulation->N && !merged; ++i) {
            for (std::size_t j = i + 1; j < simulation->N; ++j) {
                const auto& a = simulation->particles[i];
                const auto& b = simulation->particles[j];
                const double dx = a.x - b.x;
                const double dy = a.y - b.y;
                if (dx * dx + dy * dy <= (a.r + b.r) * (a.r + b.r)) {
                    reb_collision collision{};
                    collision.p1 = i;
                    collision.p2 = j;
                    Impl::merge_collision(simulation, collision);
                    if (reb_simulation_remove_particle(simulation, j) != 0) {
                        throw std::runtime_error("Could not remove merged particle");
                    }
                    merged = true;
                    break;
                }
            }
        }
    }
    impl_->record_positions();
}

Simulation::~Simulation() = default;
Simulation::Simulation(std::unique_ptr<Impl> impl):impl_(std::move(impl)) {}
std::unique_ptr<Simulation> Simulation::clone() const {
    auto copy=std::make_unique<Impl>();
    copy->simulation=reb_simulation_create();
    if(!copy->simulation) throw std::runtime_error("Could not allocate simulation checkpoint");
    auto warnings=REB_BINARYDATA_WARNING_NONE;
    reb_simulation_copy_with_messages(copy->simulation,impl_->simulation,&warnings);
    // Function pointers are intentionally omitted by REBOUND's state serialization.
    if(int(warnings)&~int(REB_BINARYDATA_WARNING_POINTERS))
        throw std::runtime_error("Could not copy complete simulation state");
    copy->events=impl_->events;copy->trails=impl_->trails;copy->identity_names=impl_->identity_names;
    copy->simulation->extras=copy.get();
    copy->simulation->collision_resolve=&Impl::merge_collision;
    for(std::size_t i=0;i<copy->simulation->N;++i) {
        const std::string id=impl_->simulation->particles[i].name?impl_->simulation->particles[i].name:"";
        const auto name=std::find(copy->identity_names.begin(),copy->identity_names.end(),id);
        if(name==copy->identity_names.end()) throw std::runtime_error("Missing checkpoint body identity");
        reb_particle_set_name(&copy->simulation->particles[i],name->c_str());
    }
    return std::unique_ptr<Simulation>(new Simulation(std::move(copy)));
}

void Simulation::advance_to(double time) {
    if (!std::isfinite(time) || time < impl_->simulation->t || time > 100000.0) {
        throw std::invalid_argument("Target time must be finite, nonnegative, and not earlier than current time");
    }
    if (time == impl_->simulation->t) return;
    const double start = impl_->simulation->t;
    const int samples = std::clamp(static_cast<int>(std::ceil(std::min(time - start, 256.0) / 0.025)), 1, 256);
    for (int i = 1; i <= samples; ++i) {
        const auto status = reb_simulation_integrate(impl_->simulation,
                                                     start + (time - start) * i / samples);
        if (status != REB_STATUS_SUCCESS) throw std::runtime_error("REBOUND integration did not finish");
        impl_->record_positions();
    }
}

SceneSnapshot Simulation::snapshot() const {
    SceneSnapshot snapshot;
    snapshot.time = impl_->simulation->t;
    snapshot.events = impl_->events;
    snapshot.trails = impl_->trails;
    for (std::size_t i = 0; i < impl_->simulation->N; ++i) {
        const reb_particle& particle = impl_->simulation->particles[i];
        snapshot.bodies.push_back({particle.name ? particle.name : "", {particle.x, particle.y},
                                   {particle.vx, particle.vy}, particle.m, particle.r});
    }
    return snapshot;
}

} // namespace planet_sandbox
