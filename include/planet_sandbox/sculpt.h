#pragma once

#include "planet_sandbox/renderer.h"
#include "planet_sandbox/texture.h"

#include <vector>

namespace planet_sandbox {

constexpr int sculpt_columns = 96;
constexpr int sculpt_rows = 49;

enum class SculptMode { Raise, Lower, Smooth };

double sculpt_offset(const DesignOverrides& design, SolarShapeLayer layer, double u, double v);
void apply_sculpt_brush(DesignOverrides& design, BodyKind kind, SolarShapeLayer layer,
                        SculptMode mode, double u, double v, double radius, double strength);

class SculptHistory {
public:
    void begin_stroke(BodyKind kind, SolarShapeLayer layer, const DesignOverrides& design);
    bool undo(BodyKind kind, DesignOverrides& design);
    void clear_body(BodyKind kind);
    void clear();

private:
    struct Entry {
        BodyKind kind;
        SolarShapeLayer layer;
        std::vector<float> before;
    };
    std::vector<Entry> entries_;
};

class SculptStroke {
public:
    bool begin(const RenderFrame& frame, int body_index, int x, int y);
    std::vector<Vec2> move(const RenderFrame& frame, int body_index, int x, int y);
    void end();

private:
    bool active_ = false;
    int last_x_ = 0;
    int last_y_ = 0;
    int last_width_ = 0;
    int last_height_ = 0;
};

} // namespace planet_sandbox
