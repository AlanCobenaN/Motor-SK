#pragma once

#include "../math/math.h"
#include "../platform/window.h"

namespace sk {

// Camara en modo vuelo libre (estilo editor 3D):
//   WASD/Shift -> moverse, click derecho -> mirar, rueda -> acercar/alejar.
class Camera {
public:
    void update(float dt, const Window& window) {
        const float dtScale = dt;

        // Mirar con el botón derecho.
        if (window.mouseRightDown()) {
            const Vec2 d = window.consumeMouseDelta();
            yaw_ += d.x * kLookSpeed;
            pitch_ -= d.y * kLookSpeed;
            if (pitch_ > kMaxPitch) pitch_ = kMaxPitch;
            if (pitch_ < -kMaxPitch) pitch_ = -kMaxPitch;
        }

        // W/S adelante/atras, A/D strafe, Q/E bajar/subir, Shift rapido.
        Vec3 right = cross(forward(), kUp);
        Vec3 move{};
        if (window.keyDown('W')) move += forward();
        if (window.keyDown('S')) move -= forward();
        if (window.keyDown('D')) move += right;
        if (window.keyDown('A')) move -= right;
        if (window.keyDown('E')) move += kUp;
        if (window.keyDown('Q')) move -= kUp;

        float speed = kMoveSpeed;
        if (window.keyDown(kKeyShift)) speed *= 3.0f;

        if (length(move) > 0.0f) {
            position_ += normalize(move) * (speed * dtScale);
        }

        // Rueda: acercar/alejar a lo largo de la direccion de la mirada.
        const float wheel = window.consumeWheel();
        if (wheel != 0.0f) {
            position_ += forward() * (wheel * kZoomStep);
        }
    }

    Vec3 forward() const {
        const float cp = std::cos(pitch_);
        return normalize(Vec3{
            std::cos(yaw_) * cp,
            std::sin(pitch_),
            std::sin(yaw_) * cp,
        });
    }

    Mat4 view() const { return lookAt(position_, position_ + forward(), kUp); }

    const Vec3& position() const { return position_; }

    // Yaw/pitch en radianes; direccion -Z al inicio.
    float yaw_ = -kPi * 0.5f;
    float pitch_ = -0.15f;

private:
    static constexpr float kMoveSpeed = 6.0f;    // unidades por segundo
    static constexpr float kLookSpeed = 0.0025f; // radianes por pixel
    static constexpr float kZoomStep = 0.6f;     // unidades por tick de rueda
    static constexpr float kMaxPitch = 1.5f;     // ~86 grados
    static constexpr int kKeyShift = 0x10;       // VK_SHIFT

    static const Vec3 kUp;

    Vec3 position_{0.0f, 2.5f, 6.0f};
};

inline const Vec3 Camera::kUp{0.0f, 1.0f, 0.0f};

} // namespace sk
