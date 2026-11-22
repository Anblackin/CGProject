// Module C - minimal linear algebra for the software rasterizer.
// Conventions agreed with the team:
//   * Right-handed world; Y is up; orbital plane is XZ; Sun at world origin.
//   * Column-major 4x4 matrices stored in a flat array; vectors are columns: v' = M * v.
//   * Composition: World = T * R; Clip = P * V * World (applied right to left).
//   * Distances are in "scene units" (display scale), NOT physical astronomical units.
#pragma once

#include <cmath>
#include <cstddef>

namespace solarc {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float xx, float yy) : x(xx), y(yy) {}
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    constexpr Vec3() = default;
    constexpr Vec3(float xx, float yy, float zz) : x(xx), y(yy), z(zz) {}

    constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(float s) const noexcept { return {x / s, y / s, z / s}; }
    constexpr Vec3 operator-() const noexcept { return {-x, -y, -z}; }

    constexpr float dot(const Vec3& o) const noexcept { return x * o.x + y * o.y + z * o.z; }

    constexpr Vec3 cross(const Vec3& o) const noexcept {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }

    float length() const noexcept { return std::sqrt(x * x + y * y + z * z); }

    Vec3 normalized() const noexcept {
        const float l = length();
        return l > 1e-8f ? (*this) / l : Vec3{};
    }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) noexcept { return v * s; }

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    constexpr Vec4() = default;
    constexpr Vec4(float xx, float yy, float zz, float ww) : x(xx), y(yy), z(zz), w(ww) {}
    explicit constexpr Vec4(const Vec3& v, float ww = 1.0f) : x(v.x), y(v.y), z(v.z), w(ww) {}
};

// Column-major 4x4: m[column * 4 + row].
struct Mat4 {
    float m[16]{};

    static Mat4 identity() noexcept {
        Mat4 r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }

    // this * o
    Mat4 operator*(const Mat4& o) const noexcept {
        Mat4 r{};
        for (int c = 0; c < 4; ++c) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += m[k * 4 + row] * o.m[c * 4 + k];
                }
                r.m[c * 4 + row] = sum;
            }
        }
        return r;
    }

    Vec4 operator*(const Vec4& v) const noexcept {
        return {
            m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w
        };
    }

    Vec3 transformPoint(const Vec3& v) const noexcept {
        const Vec4 c = (*this) * Vec4(v, 1.0f);
        return {c.x / c.w, c.y / c.w, c.z / c.w};
    }

    Vec3 transformDirection(const Vec3& v) const noexcept {
        const Vec4 c = (*this) * Vec4(v, 0.0f);
        return {c.x, c.y, c.z};
    }

    static Mat4 translation(float x, float y, float z) noexcept {
        Mat4 r = identity();
        r.m[12] = x; r.m[13] = y; r.m[14] = z;
        return r;
    }

    static Mat4 scaling(float x, float y, float z) noexcept {
        Mat4 r = identity();
        r.m[0] = x; r.m[5] = y; r.m[10] = z;
        return r;
    }

    static Mat4 rotationX(float rad) noexcept {
        Mat4 r = identity();
        const float c = std::cos(rad), s = std::sin(rad);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }

    static Mat4 rotationY(float rad) noexcept {
        Mat4 r = identity();
        const float c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }

    static Mat4 rotationZ(float rad) noexcept {
        Mat4 r = identity();
        const float c = std::cos(rad), s = std::sin(rad);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }

    // Right-handed look-at. Camera looks down its local -Z.
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& upHint) noexcept {
        const Vec3 f = (target - eye).normalized();          // forward
        Vec3 up = upHint;
        Vec3 s = f.cross(up).normalized();                    // right
        if (s.length() < 1e-6f) {                             // degenerate up
            up = {0.0f, 0.0f, 1.0f};
            s = f.cross(up).normalized();
        }
        const Vec3 u = s.cross(f);                            // true up

        Mat4 r = identity();
        // Rows of the view rotation are the camera basis; column-major storage:
        r.m[0] = s.x; r.m[4] = s.y; r.m[8]  = s.z; r.m[12] = -s.dot(eye);
        r.m[1] = u.x; r.m[5] = u.y; r.m[9]  = u.z; r.m[13] = -u.dot(eye);
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z; r.m[14] = f.dot(eye);
        return r;
    }

    // OpenGL-style symmetric perspective; clip z in [-w, w].
    static Mat4 perspective(float fovYRad, float aspect, float zNear, float zFar) noexcept {
        Mat4 r{};
        const float f = 1.0f / std::tan(fovYRad * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
        return r;
    }
};

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

} // namespace solarc
