#include "planet_sandbox/renderer.h"

#include <algorithm>
#include <stdexcept>

namespace planet_sandbox {

Image render_preview(const Project& project, const SceneSnapshot& snapshot, int width, int height) {
    RenderConfig config;
    config.width = width;
    config.height = height;
    return render_scene(project, snapshot, config).image;
}

RenderFrame render_comparison_scene(const Project& project, const SceneSnapshot& snapshot,
                                    const RenderConfig& config, SamplingMode left,
                                    SamplingMode right, bool right_first,const Project* right_project) {
    if (config.width <= 0 || config.height <= 0 || config.width > 4096 ||
        static_cast<std::size_t>(config.width) * config.height * 2u > 8000000u ||
        ((config.trace_x != -1 || config.trace_y != -1) &&
         (config.trace_x < 0 || config.trace_x >= config.width * 2 ||
          config.trace_y < 0 || config.trace_y >= config.height))) {
        throw std::invalid_argument("Invalid comparison size or trace pixel");
    }
    RenderConfig left_config = config;
    RenderConfig right_config = config;
    left_config.sampling = left;
    right_config.sampling = right;
    const bool tracing_left = config.trace_x >= 0 && config.trace_x < config.width;
    const bool tracing_right = config.trace_x >= config.width;
    if (tracing_left) {
        right_config.trace_x = right_config.trace_y = -1;
    } else if (tracing_right) {
        left_config.trace_x = left_config.trace_y = -1;
        right_config.trace_x -= config.width;
    }
    RenderFrame left_frame, right_frame;
    if (right_first) {
        right_frame = render_scene(right_project?*right_project:project, snapshot, right_config);
        left_frame = render_scene(project, snapshot, left_config);
    } else {
        left_frame = render_scene(project, snapshot, left_config);
        right_frame = render_scene(right_project?*right_project:project, snapshot, right_config);
    }
    RenderFrame combined;
    combined.image.width = config.width * 2;
    combined.image.height = config.height;
    const std::size_t pixels = static_cast<std::size_t>(combined.image.width) * combined.image.height;
    combined.image.rgb.resize(pixels * 3);
    combined.body_index.resize(pixels);
    combined.surface_uv.resize(pixels);
    combined.sample_surface_variation.resize(pixels);
    combined.shade_evaluations = left_frame.shade_evaluations + right_frame.shade_evaluations;
    combined.panel_shade_evaluations = {left_frame.shade_evaluations,
                                        right_frame.shade_evaluations};
    combined.panel_count = 2;
    combined.panel_sampling = {left, right};
    combined.panel_render_ms = {left_frame.render_ms, right_frame.render_ms};
    combined.render_ms = left_frame.render_ms + right_frame.render_ms;
    for (int y = 0; y < config.height; ++y) {
        const std::size_t source = static_cast<std::size_t>(y) * config.width;
        const std::size_t target = static_cast<std::size_t>(y) * combined.image.width;
        for (int panel = 0; panel < 2; ++panel) {
            const RenderFrame& frame = panel == 0 ? left_frame : right_frame;
            const std::size_t destination = target + panel * config.width;
            std::copy_n(frame.image.rgb.begin() + source * 3, config.width * 3,
                        combined.image.rgb.begin() + destination * 3);
            std::copy_n(frame.body_index.begin() + source, config.width,
                        combined.body_index.begin() + destination);
            std::copy_n(frame.surface_uv.begin() + source, config.width,
                        combined.surface_uv.begin() + destination);
            std::copy_n(frame.sample_surface_variation.begin() + source, config.width,
                        combined.sample_surface_variation.begin() + destination);
        }
    }
    if (tracing_left) combined.trace = left_frame.trace;
    if (tracing_right) {
        combined.trace = right_frame.trace;
        combined.trace->x += config.width;
    }
    return combined;
}

void write_ppm(std::ostream& output, const Image& image) {
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    output.write(reinterpret_cast<const char*>(image.rgb.data()),
                 static_cast<std::streamsize>(image.rgb.size()));
    if (!output) throw std::runtime_error("Could not write PPM image");
}

} // namespace planet_sandbox
