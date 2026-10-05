#pragma once

#include <cmath>

namespace sk {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    if (len <= 1e-8f) return {0.0f, 0.0f, 0.0f};
    return v * (1.0f / len);
}

// Matriz 4x4 almacenada en columna mayor (estilo OpenGL/Vulkan),
// compatible con mat4 en los shaders con std140/std430.
struct Mat4 {
    float m[16] = {};

    float& at(int col, int row) { return m[col * 4 + row]; }
    float at(int col, int row) const { return m[col * 4 + row]; }

    static Mat4 identity() {
        Mat4 r;
        r.at(0, 0) = 1.0f;
        r.at(1, 1) = 1.0f;
        r.at(2, 2) = 1.0f;
        r.at(3, 3) = 1.0f;
        return r;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.at(k, row) * b.at(c, k);
            }
            r.at(c, row) = sum;
        }
    }
    return r;
}

inline constexpr float kPi = 3.14159265358979323846f;

inline float radians(float degrees) { return degrees * (kPi / 180.0f); }

inline Mat4 translate(const Vec3& t) {
    Mat4 r = Mat4::identity();
    r.at(3, 0) = t.x;
    r.at(3, 1) = t.y;
    r.at(3, 2) = t.z;
    return r;
}

inline Mat4 scale(const Vec3& s) {
    Mat4 r = Mat4::identity();
    r.at(0, 0) = s.x;
    r.at(1, 1) = s.y;
    r.at(2, 2) = s.z;
    return r;
}

// Rotaciones elementales (grados, dextrorasas, almacenamiento columna
// mayor como el resto del archivo).
inline Mat4 rotateX(float degrees) {
    const float c = std::cos(radians(degrees));
    const float s = std::sin(radians(degrees));
    Mat4 r = Mat4::identity();
    r.at(1, 1) = c;
    r.at(1, 2) = -s;
    r.at(2, 1) = s;
    r.at(2, 2) = c;
    return r;
}

inline Mat4 rotateY(float degrees) {
    const float c = std::cos(radians(degrees));
    const float s = std::sin(radians(degrees));
    Mat4 r = Mat4::identity();
    r.at(0, 0) = c;
    r.at(2, 0) = s;
    r.at(0, 2) = -s;
    r.at(2, 2) = c;
    return r;
}

inline Mat4 rotateZ(float degrees) {
    const float c = std::cos(radians(degrees));
    const float s = std::sin(radians(degrees));
    Mat4 r = Mat4::identity();
    r.at(0, 0) = c;
    r.at(1, 0) = -s;
    r.at(0, 1) = s;
    r.at(1, 1) = c;
    return r;
}

// Proyeccion en espacio de clip de Vulkan (Y hacia abajo, Z en [0,1]):
// el eje Y se invierte respecto a la convencion de OpenGL.
inline Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar) {
    Mat4 r;
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    r.at(0, 0) = f / aspect;
    r.at(1, 1) = -f;
    r.at(2, 2) = zFar / (zNear - zFar);
    r.at(2, 3) = -1.0f;
    r.at(3, 2) = (zNear * zFar) / (zNear - zFar);
    return r;
}

// Vista Vulkan (Y invertido respecto a OpenGL, clip Z en [0,1]).
inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    Mat4 r = Mat4::identity();
    r.at(0, 0) = s.x;  r.at(1, 0) = s.y;  r.at(2, 0) = s.z;
    r.at(0, 1) = u.x;  r.at(1, 1) = u.y;  r.at(2, 1) = u.z;
    r.at(0, 2) = -f.x; r.at(1, 2) = -f.y; r.at(2, 2) = -f.z;
    r.at(3, 0) = -dot(s, eye);
    r.at(3, 1) = -dot(u, eye);
    r.at(3, 2) = dot(f, eye);
    return r;
}

} // namespace sk
