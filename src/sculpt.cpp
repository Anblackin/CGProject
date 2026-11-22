#include "planet_sandbox/sculpt.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace planet_sandbox {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double max_offset = 0.08;

const std::vector<float>& map_for(const DesignOverrides& design, SolarShapeLayer layer) {
    return layer == SolarShapeLayer::VenusRadar ? design.sculpt_radar : design.sculpt_visible;
}

std::vector<float>& map_for(DesignOverrides& design, SolarShapeLayer layer) {
    return layer == SolarShapeLayer::VenusRadar ? design.sculpt_radar : design.sculpt_visible;
}

double angular_distance(double u1, double v1, double u2, double v2) {
    const double a = pi * v1, b = pi * v2;
    const double cosine = std::cos(a) * std::cos(b) +
                          std::sin(a) * std::sin(b) * std::cos(2.0 * pi * (u1 - u2));
    return std::acos(std::clamp(cosine, -1.0, 1.0));
}

} // namespace

double sculpt_offset(const DesignOverrides& design, SolarShapeLayer layer, double u, double v) {
    const auto& map = map_for(design, layer);
    if (map.empty()) return 0.0;
    const double x = (u - std::floor(u)) * sculpt_columns;
    const double y = std::clamp(v, 0.0, 1.0) * (sculpt_rows - 1);
    const int col = static_cast<int>(x);
    const int next_col = (col + 1) % sculpt_columns;
    const int row = static_cast<int>(y);
    const int next_row = std::min(row + 1, sculpt_rows - 1);
    const double fx = x - col, fy = y - row;
    const auto sample = [&](int r, int c) { return map[r * sculpt_columns + c]; };
    const double top = sample(row, col) * (1.0 - fx) + sample(row, next_col) * fx;
    const double bottom = sample(next_row, col) * (1.0 - fx) + sample(next_row, next_col) * fx;
    return top * (1.0 - fy) + bottom * fy;
}

void apply_sculpt_brush(DesignOverrides& design, BodyKind kind, SolarShapeLayer layer,
                        SculptMode mode, double u, double v, double radius, double strength) {
    if (kind == BodyKind::Custom || (layer == SolarShapeLayer::VenusRadar && kind != BodyKind::Venus) ||
        !std::isfinite(u) || !std::isfinite(v) || !std::isfinite(radius) ||
        !std::isfinite(strength) || v < 0.0 || v > 1.0 || radius <= 0.0 ||
        radius > pi || strength < 0.0 || strength > 1.0) {
        throw std::invalid_argument("Invalid sculpt brush");
    }
    const DesignOverrides before = design;
    auto& map = map_for(design, layer);
    if (map.empty()) map.assign(sculpt_columns * sculpt_rows, 0.0f);
    for (int row = 0; row < sculpt_rows; ++row) {
        const double cell_v = static_cast<double>(row) / (sculpt_rows - 1);
        for (int col = 0; col < sculpt_columns; ++col) {
            const double cell_u = static_cast<double>(col) / sculpt_columns;
            const double distance = angular_distance(cell_u, cell_v, u, v);
            if (distance >= radius) continue;
            const double t = 1.0 - distance / radius;
            const double weight = t * t * (3.0 - 2.0 * t);
            const std::size_t index = row * sculpt_columns + col;
            double change = 0.0;
            if (mode == SculptMode::Raise || mode == SculptMode::Lower) {
                change = (mode == SculptMode::Raise ? 1.0 : -1.0) * 0.0025 * strength * weight;
            } else {
                const double center = solar_shape_multiplier(kind, cell_u, cell_v, before, layer);
                const double left = solar_shape_multiplier(kind, cell_u - 1.0 / sculpt_columns,
                                                           cell_v, before, layer);
                const double right = solar_shape_multiplier(kind, cell_u + 1.0 / sculpt_columns,
                                                            cell_v, before, layer);
                const double above = solar_shape_multiplier(kind, cell_u,
                                                            std::max(0.0, cell_v - 1.0 / (sculpt_rows - 1)),
                                                            before, layer);
                const double below = solar_shape_multiplier(kind, cell_u,
                                                            std::min(1.0, cell_v + 1.0 / (sculpt_rows - 1)),
                                                            before, layer);
                change = ((left + right + above + below) * 0.25 - center) * strength * weight;
            }
            map[index] = static_cast<float>(std::clamp(static_cast<double>(map[index]) + change,
                                                       -max_offset, max_offset));
        }
    }
    for (int col = 1; col < sculpt_columns; ++col) {
        map[col] = map[0];
        map[(sculpt_rows - 1) * sculpt_columns + col] = map[(sculpt_rows - 1) * sculpt_columns];
    }
}

void SculptHistory::begin_stroke(BodyKind kind, SolarShapeLayer layer,
                                 const DesignOverrides& design) {
    if (entries_.size() == 32) entries_.erase(entries_.begin());
    entries_.push_back({kind, layer, map_for(design, layer)});
}

bool SculptHistory::undo(BodyKind kind, DesignOverrides& design) {
    const auto found = std::find_if(entries_.rbegin(), entries_.rend(),
                                    [&](const Entry& entry) { return entry.kind == kind; });
    if (found == entries_.rend()) return false;
    map_for(design, found->layer) = found->before;
    entries_.erase(std::next(found).base());
    return true;
}

void SculptHistory::clear_body(BodyKind kind) {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Entry& entry) { return entry.kind == kind; }),
                   entries_.end());
}

void SculptHistory::clear() { entries_.clear(); }

namespace {

bool pick_surface(const RenderFrame& frame, int body_index, int x, int y, Vec2& uv) {
    if (x < 0 || y < 0 || x >= frame.image.width || y >= frame.image.height) return false;
    const std::size_t pixel = static_cast<std::size_t>(y) * frame.image.width + x;
    if (pixel >= frame.body_index.size() || pixel >= frame.surface_uv.size() ||
        frame.body_index[pixel] != body_index) return false;
    uv = frame.surface_uv[pixel];
    return std::isfinite(uv.x) && std::isfinite(uv.y) && uv.x >= 0.0 &&
           uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0;
}

} // namespace

bool SculptStroke::begin(const RenderFrame& frame, int body_index, int x, int y) {
    end();
    Vec2 uv;
    if (!pick_surface(frame, body_index, x, y, uv)) return false;
    active_ = true;
    last_x_ = x;
    last_y_ = y;
    last_width_ = frame.image.width;
    last_height_ = frame.image.height;
    return true;
}

std::vector<Vec2> SculptStroke::move(const RenderFrame& frame, int body_index, int x, int y) {
    std::vector<Vec2> path;
    if (!active_) return path;
    // Preview refinement can change pixel dimensions during one continuous stroke.
    if (frame.image.width != last_width_ || frame.image.height != last_height_) {
        last_x_ = static_cast<int>(std::lround(
            (last_x_ + 0.5) * frame.image.width / last_width_ - 0.5));
        last_y_ = static_cast<int>(std::lround(
            (last_y_ + 0.5) * frame.image.height / last_height_ - 0.5));
        last_width_ = frame.image.width;
        last_height_ = frame.image.height;
    }
    const int dx = x - last_x_, dy = y - last_y_;
    const int steps = static_cast<int>(std::ceil(std::hypot(dx, dy) / 6.0));
    for (int step = 1; step <= steps; ++step) {
        const double fraction = static_cast<double>(step) / steps;
        const int px = static_cast<int>(std::lround(last_x_ + dx * fraction));
        const int py = static_cast<int>(std::lround(last_y_ + dy * fraction));
        Vec2 uv;
        if (pick_surface(frame, body_index, px, py, uv)) path.push_back(uv);
    }
    last_x_ = x;
    last_y_ = y;
    return path;
}

void SculptStroke::end() { active_ = false; }

} // namespace planet_sandbox
