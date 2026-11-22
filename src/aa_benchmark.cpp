#include "planet_sandbox/aa_benchmark.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace planet_sandbox {
namespace {

const BodySpec& find_body(const Project& project, const char* id) {
    const auto found = std::find_if(project.bodies.begin(), project.bodies.end(),
                                    [&](const BodySpec& body) { return body.id == id; });
    if (found == project.bodies.end()) throw std::invalid_argument(std::string("Missing body: ") + id);
    return *found;
}

void check_image(const Image& image) {
    if (image.width <= 0 || image.height <= 0 ||
        image.rgb.size() != static_cast<std::size_t>(image.width) * image.height * 3u) {
        throw std::invalid_argument("Invalid RGB image");
    }
}

} // namespace

AABenchmarkScene make_aa_benchmark_scene(const Project& source, AAScene scene,
                                         int width, int height) {
    validate_project(source);
    if (!source.solar_system) throw std::invalid_argument("Benchmark requires a solar-system project");
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        static_cast<std::size_t>(width) * height * 16u > 8000000u) {
        throw std::invalid_argument("Invalid benchmark dimensions");
    }
    AABenchmarkScene result;
    result.project = source;
    result.config.width = width;
    result.config.height = height;
    if (scene == AAScene::Orbit) {
        result.name = "orbit";
        result.project.view_extent = 1.2;
        BodyTrail line;
        line.id = "earth";
        constexpr int segments = 128;
        constexpr double pi = 3.14159265358979323846;
        for (int step = 0; step <= segments; ++step) {
            const double angle = 2.0 * pi * step / segments;
            line.points.push_back({0.78 * std::cos(angle), 0.78 * std::sin(angle)});
        }
        result.snapshot.reference_trails.push_back(std::move(line));
        return result;
    }
    const char* id = scene == AAScene::Earth ? "earth" : "saturn";
    const BodySpec& body = find_body(result.project, id);
    result.name = id;
    result.snapshot.bodies.push_back({body.id, {0.0, 0.0}, {}, body.mass, body.radius});
    const double framing = scene == AAScene::Earth ? 0.95 : 2.0;
    result.project.view_extent = body.display_radius * result.project.display_scale * framing;
    result.config.camera.yaw = scene == AAScene::Earth ? 0.36 : 0.0;
    result.config.camera.pitch = scene == AAScene::Earth ? 0.14 : 0.2;
    return result;
}

std::vector<std::uint8_t> make_aa_edge_mask(const RenderFrame& reference) {
    check_image(reference.image);
    const int width = reference.image.width, height = reference.image.height;
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    if (reference.sample_surface_variation.size() != pixels) {
        throw std::invalid_argument("Reference lacks per-sample geometry data");
    }
    std::vector<std::uint8_t> mask(pixels, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (!reference.sample_surface_variation[static_cast<std::size_t>(y) * width + x]) continue;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                        mask[static_cast<std::size_t>(ny) * width + nx] = 1;
                    }
                }
            }
        }
    }
    if (std::none_of(mask.begin(), mask.end(), [](std::uint8_t value) { return value != 0; })) {
        throw std::invalid_argument("Reference has no geometric edges");
    }
    return mask;
}

AAError measure_aa_error(const Image& candidate, const Image& reference,
                         const std::vector<std::uint8_t>& edge_mask) {
    check_image(candidate);
    check_image(reference);
    if (candidate.width != reference.width || candidate.height != reference.height ||
        edge_mask.size() != static_cast<std::size_t>(reference.width) * reference.height) {
        throw std::invalid_argument("Benchmark images and mask must have the same dimensions");
    }
    AAError result;
    double edge_sum = 0.0, full_sum = 0.0;
    const std::size_t pixels = edge_mask.size();
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        if (edge_mask[pixel]) ++result.edge_pixels;
        for (int channel = 0; channel < 3; ++channel) {
            const std::size_t index = pixel * 3 + channel;
            const double a = std::pow(candidate.rgb[index] / 255.0, 2.2);
            const double b = std::pow(reference.rgb[index] / 255.0, 2.2);
            const double difference = std::abs(a - b);
            full_sum += difference;
            if (edge_mask[pixel]) edge_sum += difference;
        }
    }
    if (result.edge_pixels == 0) throw std::invalid_argument("Benchmark edge mask is empty");
    result.edge_mae_linear = edge_sum / (result.edge_pixels * 3);
    result.full_mae_linear = full_sum / (pixels * 3);
    return result;
}

} // namespace planet_sandbox
