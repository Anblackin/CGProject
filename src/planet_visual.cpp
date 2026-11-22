#include "planet_sandbox/planet_visual.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>

namespace planet_sandbox {
namespace {
constexpr double pi = 3.14159265358979323846;
double wrap(double u) { return u - std::floor(u); }

double stretch_latitude(double v, double center, double factor) {
    const double side = v < center ? center : 1.0 - center;
    const double x = (v - center) / side;
    return center + side * factor * x / (1.0 + (factor - 1.0) * std::abs(x));
}
} // namespace

Vec2 solar_visual_uv(BodyKind kind, double u, double v, const DesignOverrides& d) {
    u = wrap(u);
    v = std::clamp(v, 0.0, 1.0);
    if (kind != BodyKind::Jupiter && kind != BodyKind::Saturn &&
        kind != BodyKind::Uranus && kind != BodyKind::Neptune) return {u, v};
    if (v == 0.0 || v == 1.0) return {u, v};
    if (kind == BodyKind::Jupiter || kind == BodyKind::Neptune) {
        const double source_u = kind == BodyKind::Jupiter ? 0.37 : 0.19;
        const double source_v = kind == BodyKind::Jupiter ? 0.61 : 0.16;
        if (d.storm_longitude != source_u || d.storm_latitude != source_v ||
            d.storm_aspect != 1.0 || d.storm_scale != 1.0) {
            const double size = std::max(0.15, d.storm_scale);
            const double angle = pi * std::remainder(u - d.storm_longitude, 1.0);
            u = wrap(source_u + std::atan2(std::sin(angle) / (size * std::sqrt(d.storm_aspect)),
                                           std::cos(angle)) / pi);
            // A monotone rational transform fixes both poles and relocates one storm.
            const double a = source_v * (1.0 - d.storm_latitude);
            const double b = d.storm_latitude * (1.0 - source_v);
            v = a * v / (b + (a - b) * v);
            v = stretch_latitude(v, source_v, std::sqrt(d.storm_aspect) / size);
        }
    }
    if (v <= 0.0) return {u, 0.0};
    if (v >= 1.0) return {u, 1.0};
    if (d.band_warp > 0.0) {
        const double envelope = std::sin(pi * v);
        v += d.band_warp * 0.025 * envelope * envelope *
             (std::sin(6.0 * pi * u + 9.0 * v) + 0.35 * std::sin(14.0 * pi * u - 13.0 * v));
    }
    return {u, std::clamp(v, 0.0, 1.0)};
}

double solar_storm_mask(BodyKind kind, double u, double v) {
    if (kind != BodyKind::Jupiter && kind != BodyKind::Neptune) return 0.0;
    const bool jupiter = kind == BodyKind::Jupiter;
    const double du = std::remainder(u - (jupiter ? 0.37 : 0.19), 1.0) / (jupiter ? 0.05 : 0.075);
    const double dv = (v - (jupiter ? 0.61 : 0.16)) / (jupiter ? 0.045 : 0.055);
    return std::exp(-0.5 * (du * du + dv * dv));
}

const VisualDetailMap& solar_detail_map(BodyKind kind, SolarShapeLayer layer) {
    static std::array<VisualDetailMap, 20> cache;
    static std::array<std::once_flag, 20> flags;
    const auto index = static_cast<std::size_t>(kind) * 2 +
                       (layer == SolarShapeLayer::VenusRadar ? 1 : 0);
    auto& map = cache.at(index);
    std::call_once(flags.at(index), [&] {
        map.width = 512; map.height = 256;
        std::vector<double> residual(map.width * map.height);
        const auto height_at = [&](double u, double v) {
            return layer == SolarShapeLayer::VenusRadar ? solar_venus_radar_height_sample(u, v)
                                                       : solar_height_sample(kind, u, v);
        };
        for (int y = 0; y < map.height; ++y) {
            for (int x = 0; x < map.width; ++x) {
                const double u = double(x) / map.width, v = double(y) / (map.height - 1);
                double low = 0;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
                    low += height_at(u + dx / 96.0, v + dy / 48.0) / 9.0;
                residual[y * map.width + x] = height_at(u, v) - low;
            }
        }
        map.gradient.resize(residual.size());
        for (int y = 0; y < map.height; ++y) for (int x = 0; x < map.width; ++x) {
            map.gradient[y * map.width + x] = {
                (residual[y * map.width + (x + 1) % map.width] -
                 residual[y * map.width + (x + map.width - 1) % map.width]) * map.width * 0.5,
                (residual[std::min(y + 1, map.height - 1) * map.width + x] -
                 residual[std::max(y - 1, 0) * map.width + x]) * (map.height - 1) * 0.5};
        }
    });
    return map;
}

Vec2 sample_visual_detail(const VisualDetailMap& map, double u, double v) {
    const double x = wrap(u) * map.width, y = std::clamp(v, 0.0, 1.0) * (map.height - 1);
    const int x0 = static_cast<int>(x) % map.width, y0 = static_cast<int>(y);
    const int x1 = (x0 + 1) % map.width, y1 = std::min(y0 + 1, map.height - 1);
    const double fx = x - std::floor(x), fy = y - y0;
    const auto a = map.gradient[y0 * map.width + x0], b = map.gradient[y0 * map.width + x1];
    const auto c = map.gradient[y1 * map.width + x0], d = map.gradient[y1 * map.width + x1];
    return {(a.x * (1 - fx) + b.x * fx) * (1 - fy) + (c.x * (1 - fx) + d.x * fx) * fy,
            (a.y * (1 - fx) + b.y * fx) * (1 - fy) + (c.y * (1 - fx) + d.y * fx) * fy};
}

VisualVector perturb_visual_normal(VisualVector normal, VisualVector radial, VisualVector pole,
                                    double longitude_slope, double latitude_slope) {
    const auto cross = [](VisualVector a, VisualVector b) -> VisualVector {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    const auto unit = [](VisualVector p) -> VisualVector {
        const double length = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        return length > 1e-12 ? VisualVector{p.x / length, p.y / length, p.z / length} : VisualVector{};
    };
    const auto along = unit(cross(radial, pole));
    const auto down = unit(cross(radial, along));
    return unit({normal.x - along.x * longitude_slope - down.x * latitude_slope,
                 normal.y - along.y * longitude_slope - down.y * latitude_slope,
                 normal.z - along.z * longitude_slope - down.z * latitude_slope});
}

bool planet_blocks_light(VisualVector p, VisualVector l, double radius) {
    const double along = p.x * l.x + p.y * l.y + p.z * l.z;
    const double perpendicular = p.x * p.x + p.y * p.y + p.z * p.z - along * along;
    return along < 0.0 && perpendicular < radius * radius;
}

double ring_plane_radius(VisualVector p, VisualVector l, VisualVector n) {
    const double denominator = l.x * n.x + l.y * n.y + l.z * n.z;
    if (std::abs(denominator) < 1e-8) return -1.0;
    const double t = -(p.x * n.x + p.y * n.y + p.z * n.z) / denominator;
    if (t <= 1e-6) return -1.0;
    p = {p.x + t * l.x, p.y + t * l.y, p.z + t * l.z};
    return std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
}

} // namespace planet_sandbox
