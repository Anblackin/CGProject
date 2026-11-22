#include "solarc/texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <vector>

namespace solarc {

float srgbChannelToLinear(float c) noexcept {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearChannelToSrgb(float c) noexcept {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

Rgba srgbToLinear(const Rgba& c) noexcept {
    return {srgbChannelToLinear(c.r), srgbChannelToLinear(c.g),
            srgbChannelToLinear(c.b), c.a};
}

Rgba linearToSrgb(const Rgba& c) noexcept {
    return {linearChannelToSrgb(c.r), linearChannelToSrgb(c.g),
            linearChannelToSrgb(c.b), c.a};
}

namespace {

int wrapIndex(int i, int n, WrapMode mode) noexcept {
    if (n <= 1) return 0;
    switch (mode) {
    case WrapMode::Repeat: {
        int r = i % n;
        return r < 0 ? r + n : r;
    }
    case WrapMode::Mirror: {
        // 0,1,2,3 -> 0,1,2,3 ; 4,5,6 -> 2,1,0 ; negatives mirror back.
        int period = 2 * n;
        int r = i % period;
        if (r < 0) r += period;
        return r < n ? r : period - r - 1;
    }
    case WrapMode::Clamp:
    default:
        return std::clamp(i, 0, n - 1);
    }
}

} // namespace

Rgba sampleTexture(const Texture& tex, float u, float v) noexcept {
    if (tex.empty()) return {1.0f, 1.0f, 1.0f, 1.0f};

    const int w = tex.width, h = tex.height;

    if (tex.filter == FilterMode::Nearest) {
        // Texture coordinates address pixel centres at (i + 0.5) / n.
        const int x = wrapIndex(static_cast<int>(std::floor(u * w)), w, tex.wrapU);
        const int y = wrapIndex(static_cast<int>(std::floor(v * h)), h, tex.wrapV);
        return tex.pixels[static_cast<std::size_t>(y) * w + x];
    }

    const float fx = u * static_cast<float>(w) - 0.5f;
    const float fy = v * static_cast<float>(h) - 0.5f;
    const int x0i = static_cast<int>(std::floor(fx));
    const int y0i = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0i);
    const float ty = fy - static_cast<float>(y0i);

    const int x0 = wrapIndex(x0i, w, tex.wrapU);
    const int x1 = wrapIndex(x0i + 1, w, tex.wrapU);
    const int y0 = wrapIndex(y0i, h, tex.wrapV);
    const int y1 = wrapIndex(y0i + 1, h, tex.wrapV);

    const Rgba& p00 = tex.pixels[static_cast<std::size_t>(y0) * w + x0];
    const Rgba& p10 = tex.pixels[static_cast<std::size_t>(y0) * w + x1];
    const Rgba& p01 = tex.pixels[static_cast<std::size_t>(y1) * w + x0];
    const Rgba& p11 = tex.pixels[static_cast<std::size_t>(y1) * w + x1];

    const auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    Rgba out;
    out.r = lerp(lerp(p00.r, p10.r, tx), lerp(p01.r, p11.r, tx), ty);
    out.g = lerp(lerp(p00.g, p10.g, tx), lerp(p01.g, p11.g, tx), ty);
    out.b = lerp(lerp(p00.b, p10.b, tx), lerp(p01.b, p11.b, tx), ty);
    out.a = lerp(lerp(p00.a, p10.a, tx), lerp(p01.a, p11.a, tx), ty);
    return out;
}

bool loadPPM(const std::string& path, Texture& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char magic[3] = {0, 0, 0};
    if (!f.read(magic, 2) || magic[0] != 'P' || magic[1] != '6') return false;

    std::function<int()> readInt = [&]() -> int {
        f >> std::ws;
        while (f.peek() == '#') {
            std::string comment;
            std::getline(f, comment);
            f >> std::ws;
        }
        int value = 0;
        f >> value;
        return value;
    };

    const int w = readInt();
    const int h = readInt();
    const int maxVal = readInt();
    if (w <= 0 || h <= 0 || maxVal != 255) return false;
    f.get(); // exactly one whitespace byte after maxval

    std::vector<unsigned char> raw(static_cast<std::size_t>(w) * h * 3);
    if (!f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()))) {
        return false;
    }

    out.width = w;
    out.height = h;
    out.wrapU = WrapMode::Repeat;
    out.wrapV = WrapMode::Clamp;
    out.filter = FilterMode::Bilinear;
    out.pixels.resize(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t dst = static_cast<std::size_t>(y) * w + x;
            const std::size_t src = dst * 3;
            out.pixels[dst] = srgbToLinear({
                static_cast<float>(raw[src]) / 255.0f,
                static_cast<float>(raw[src + 1]) / 255.0f,
                static_cast<float>(raw[src + 2]) / 255.0f,
                1.0f
            });
        }
    }
    return true;
}

bool savePPM(const std::string& path, const Texture& tex) {
    if (tex.empty()) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "P6\n" << tex.width << ' ' << tex.height << "\n255\n";
    for (const Rgba& px : tex.pixels) {
        const Rgba s = linearToSrgb(px);
        const auto byte = [](float v) -> unsigned char {
            return static_cast<unsigned char>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        const char rgb[3] = {
            static_cast<char>(byte(s.r)), static_cast<char>(byte(s.g)), static_cast<char>(byte(s.b))
        };
        f.write(rgb, 3);
    }
    return static_cast<bool>(f);
}

Texture makeCheckerTexture(int size) {
    const int n = std::max(2, size);
    Texture tex;
    tex.width = tex.height = n;
    tex.wrapU = tex.wrapV = WrapMode::Repeat;
    tex.filter = FilterMode::Nearest;
    tex.pixels.resize(static_cast<std::size_t>(n) * n);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const bool white = ((x / 2) + (y / 2)) % 2 == 0;
            const float c = white ? 1.0f : 0.0f;
            tex.pixels[static_cast<std::size_t>(y) * n + x] = srgbToLinear({c, c, c, 1.0f});
        }
    }
    return tex;
}

Texture makeRingBandTexture() {
    Texture tex;
    tex.width = 256;
    tex.height = 1;
    tex.wrapU = WrapMode::Repeat;
    tex.wrapV = WrapMode::Clamp;
    tex.filter = FilterMode::Bilinear;
    tex.pixels.resize(256);
    for (int x = 0; x < 256; ++x) {
        const float t = static_cast<float>(x) / 255.0f;
        float a = 0.78f;
        // Two Cassini-like divisions.
        if (t > 0.58f && t < 0.63f) a = 0.10f;
        if (t > 0.31f && t < 0.34f) a = 0.30f;
        a += 0.08f * std::sin(t * 90.0f);
        tex.pixels[static_cast<std::size_t>(x)] = srgbToLinear({
            std::clamp(a * 0.95f, 0.0f, 1.0f),
            std::clamp(a * 0.86f, 0.0f, 1.0f),
            std::clamp(a * 0.68f, 0.0f, 1.0f),
            1.0f
        });
    }
    return tex;
}

Texture makePlanetTestTexture(int width, int height) {
    Texture tex;
    tex.width = width;
    tex.height = height;
    tex.wrapU = WrapMode::Repeat;
    tex.wrapV = WrapMode::Clamp;
    tex.filter = FilterMode::Bilinear;
    tex.pixels.resize(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(height - 1);
        const float latitude = std::abs(v - 0.5f) * 2.0f;
        const bool polarCap = latitude > 0.86f;
        for (int x = 0; x < width; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(width - 1);
            const float band = 0.5f + 0.5f * std::sin(v * 26.0f + 1.4f * std::sin(u * kTwoPi * 2.0f));
            float r, g, b;
            if (polarCap) {
                r = g = b = 0.92f;
            } else {
                r = 0.20f + 0.45f * band;
                g = 0.34f + 0.34f * band;
                b = 0.58f + 0.22f * band;
            }
            tex.pixels[static_cast<std::size_t>(y) * width + x] = srgbToLinear({r, g, b, 1.0f});
        }
    }
    return tex;
}

} // namespace solarc
