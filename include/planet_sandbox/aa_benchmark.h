#pragma once

#include "planet_sandbox/renderer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace planet_sandbox {

enum class AAScene { Earth, Saturn, Orbit };

struct AABenchmarkScene {
    std::string name;
    Project project;
    SceneSnapshot snapshot;
    RenderConfig config;
};

struct AAError {
    double edge_mae_linear = 0.0;
    double full_mae_linear = 0.0;
    std::size_t edge_pixels = 0;
};

AABenchmarkScene make_aa_benchmark_scene(const Project& project, AAScene scene,
                                         int width, int height);
std::vector<std::uint8_t> make_aa_edge_mask(const RenderFrame& reference);
AAError measure_aa_error(const Image& candidate, const Image& reference,
                         const std::vector<std::uint8_t>& edge_mask);
void write_png(const std::string& path, const Image& image);

} // namespace planet_sandbox
