#include "planet_sandbox/texture.h"
#include "planet_sandbox/solar_system.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace planet_sandbox {
namespace {

Texture artistic_shape_map(BodyKind kind, AssetLayer layer) {
    const Texture& color = solar_texture(kind, layer);
    constexpr int width = 256;
    constexpr int height = 128;
    if (color.width < width || color.height < height ||
        color.width % width != 0 || color.height % height != 0) {
        throw std::runtime_error("Unsupported color map dimensions for artistic shape");
    }
    std::vector<double> luminance(static_cast<std::size_t>(width) * height);
    const int block_x = color.width / width;
    const int block_y = color.height / height;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0.0;
            for (int dy = 0; dy < block_y; ++dy) {
                for (int dx = 0; dx < block_x; ++dx) {
                    const std::size_t source =
                        (static_cast<std::size_t>(y * block_y + dy) * color.width +
                         x * block_x + dx) * 4;
                    sum += (0.2126 * color.rgba[source] +
                            0.7152 * color.rgba[source + 1] +
                            0.0722 * color.rgba[source + 2]) / 255.0;
                }
            }
            luminance[static_cast<std::size_t>(y) * width + x] = sum / (block_x * block_y);
        }
    }
    std::vector<double> smoothed(luminance.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int source_x = (x + dx + width) % width;
                    const int source_y = std::clamp(y + dy, 0, height - 1);
                    sum += luminance[static_cast<std::size_t>(source_y) * width + source_x];
                }
            }
            smoothed[static_cast<std::size_t>(y) * width + x] = sum / 9.0;
        }
    }
    const double mean = std::accumulate(smoothed.begin(), smoothed.end(), 0.0) / smoothed.size();
    double variance = 0.0;
    for (double value : smoothed) variance += (value - mean) * (value - mean);
    const double spread = std::max(0.01, std::sqrt(variance / smoothed.size()));
    Texture shape{width, height, std::vector<std::uint8_t>(smoothed.size() * 4)};
    for (std::size_t pixel = 0; pixel < smoothed.size(); ++pixel) {
        const double relative = std::clamp(0.5 + (smoothed[pixel] - mean) / (4.0 * spread),
                                           0.0, 1.0);
        const auto value = static_cast<std::uint8_t>(std::round(relative * 255.0));
        for (int channel = 0; channel < 3; ++channel) shape.rgba[pixel * 4 + channel] = value;
        shape.rgba[pixel * 4 + 3] = 255;
    }
    return shape;
}

const Texture& solar_shape_map(BodyKind kind, AssetLayer layer) {
    static std::mutex mutex;
    static std::map<std::pair<BodyKind, AssetLayer>, Texture> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const auto key = std::make_pair(kind, layer);
    auto found = cache.find(key);
    if (found == cache.end()) found = cache.emplace(key, artistic_shape_map(kind, layer)).first;
    return found->second;
}

} // namespace

Texture load_texture(const std::string& path) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* decoded = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!decoded) throw std::runtime_error("Could not load texture: " + path);
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        stbi_image_free(decoded);
        throw std::runtime_error("Unsupported texture dimensions: " + path);
    }
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    Texture texture{width, height, std::vector<std::uint8_t>(decoded, decoded + bytes)};
    stbi_image_free(decoded);
    return texture;
}

std::array<double, 4> sample_texture(const Texture& texture, double u, double v, bool wrap_u) {
    if (texture.width <= 0 || texture.height <= 0 ||
        texture.rgba.size() != static_cast<std::size_t>(texture.width) * texture.height * 4 ||
        !std::isfinite(u) || !std::isfinite(v)) {
        throw std::invalid_argument("Invalid texture or UV coordinate");
    }
    u = wrap_u ? u - std::floor(u) : std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double x = u * (texture.width - 1);
    const double y = v * (texture.height - 1);
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const int x1 = wrap_u ? (x0 + 1) % texture.width : std::min(x0 + 1, texture.width - 1);
    const int y1 = std::min(y0 + 1, texture.height - 1);
    const double fx = x - x0, fy = y - y0;
    std::array<double, 4> color{};
    for (int channel = 0; channel < 4; ++channel) {
        const auto at = [&](int px, int py) {
            return texture.rgba[(static_cast<std::size_t>(py) * texture.width + px) * 4 + channel] / 255.0;
        };
        color[channel] = (at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx) * (1.0 - fy) +
                         (at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx) * fy;
    }
    return color;
}

std::string solar_asset_path(BodyKind kind, AssetLayer layer) {
    std::string name;
    switch (layer) {
    case AssetLayer::Color: name = body_kind_id(kind) + std::string(".jpg"); break;
    case AssetLayer::Height:
        if (kind != BodyKind::Earth && kind != BodyKind::Mars) break;
        name = body_kind_id(kind) + std::string("_height.jpg");
        break;
    case AssetLayer::Clouds:
        if (kind == BodyKind::Earth) name = "earth_clouds.jpg";
        break;
    case AssetLayer::Ring:
        if (kind == BodyKind::Saturn) name = "saturn_ring_alpha.png";
        break;
    case AssetLayer::VenusSurface:
        if (kind == BodyKind::Venus) name = "venus_surface.jpg";
        break;
    }
    if (name.empty() || kind == BodyKind::Custom) {
        throw std::invalid_argument("No baseline texture for this body and layer");
    }
    return std::string(PLANET_ASSET_DIR) + "/" + name;
}

const Texture& solar_texture(BodyKind kind, AssetLayer layer) {
    static std::mutex mutex;
    static std::map<std::pair<BodyKind, AssetLayer>, Texture> cache;
    std::lock_guard<std::mutex> lock(mutex);
    const auto key = std::make_pair(kind, layer);
    auto found = cache.find(key);
    if (found == cache.end()) {
        found = cache.emplace(key, load_texture(solar_asset_path(kind, layer))).first;
    }
    return found->second;
}

double solar_height_sample(BodyKind kind, double u, double v) {
    if (kind == BodyKind::Earth || kind == BodyKind::Mars) {
        const double aligned_u = kind == BodyKind::Mars ? u + 0.5 : u;
        return sample_texture(solar_texture(kind, AssetLayer::Height), aligned_u, v)[0];
    }
    return sample_texture(solar_shape_map(kind, AssetLayer::Color), u, v)[0];
}

double solar_venus_radar_height_sample(double u, double v) {
    return sample_texture(solar_shape_map(BodyKind::Venus, AssetLayer::VenusSurface), u, v)[0];
}

double solar_relief_multiplier(BodyKind kind, double height, double v, double relief) {
    if (v <= 0.0 || v >= 1.0 || relief == 0.0) return 1.0;
    const double latitude_factor = std::clamp(std::sin(3.14159265358979323846 * v) / 0.2,
                                               0.0, 1.0);
    const double polar_fade = latitude_factor * latitude_factor * (3.0 - 2.0 * latitude_factor);
    if (kind == BodyKind::Earth) {
        return 1.0 + std::max(0.0, height - 0.04) * 0.012 * relief * polar_fade;
    }
    if (kind == BodyKind::Mars) {
        return 1.0 + (height - 0.5) * 0.045 * relief * polar_fade;
    }
    double amplitude = 0.0;
    switch (kind) {
    case BodyKind::Sun: amplitude = 0.024; break;
    case BodyKind::Mercury: amplitude = 0.052; break;
    case BodyKind::Venus: amplitude = 0.028; break;
    case BodyKind::Jupiter: amplitude = 0.018; break;
    case BodyKind::Saturn: amplitude = 0.026; break;
    case BodyKind::Uranus: amplitude = 0.022; break;
    case BodyKind::Neptune: amplitude = 0.030; break;
    default: throw std::invalid_argument("No relative shape for this body");
    }
    return 1.0 + (height - 0.5) * amplitude * relief * polar_fade;
}

} // namespace planet_sandbox
