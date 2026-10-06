#pragma once

#include <optional>

#include "../math/math.h"

namespace sk {

// Ventana Win32 con estado de input para la cámara libre.
// El teclado (WASD/shift) se consulta con GetAsyncKeyState; el ratón
// (clicks, rueda, deltas de movimiento) llega por mensajes.
class Window {
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(int width, int height, const char* title);
    void destroy();

    // Procesa mensajes pendientes. Devuelve false cuando se pidió cerrar.
    bool pumpEvents();

    bool shouldClose() const { return shouldClose_; }

    bool keyDown(int vk) const;

    bool mouseRightDown() const { return rmbDown_; }
    bool mouseLeftDown() const { return lmbDown_; }

    // Posición actual del ratón en coordenadas de cliente de la ventana.
    Vec2 mousePos() const { return mousePos_; }

    // Delta acumulado desde la última consulta y rueda acumulada.
    // Son "consume": vacian el acumulador para el próximo frame.
    Vec2 consumeMouseDelta() const;
    float consumeWheel() const;

    // Clicks del botón izquierdo: consumen el evento (una sola vez) con
    // la posición de cliente donde ocurrieron.
    std::optional<Vec2> consumeLeftPressed();
    std::optional<Vec2> consumeLeftReleased();

    int framebufferWidth() const { return width_; }
    int framebufferHeight() const { return height_; }

    // true una sola vez tras un WM_SIZE (para recrear el swapchain).
    bool consumeResized() {
        bool r = resized_;
        resized_ = false;
        return r;
    }

    void* nativeHandle() const { return hwnd_; }

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam);

    void* hwnd_ = nullptr;
    void* instance_ = nullptr;

    bool shouldClose_ = false;
    bool rmbDown_ = false;
    bool lmbDown_ = false;
    bool resized_ = false;

    bool tracking_ = false;
    mutable Vec2 pendingDelta_;
    mutable float pendingWheel_ = 0.0f;
    mutable std::optional<Vec2> pendingPress_;
    mutable std::optional<Vec2> pendingRelease_;
    Vec2 mousePos_{};
    int lastX_ = 0;
    int lastY_ = 0;

    int width_ = 0;
    int height_ = 0;
};

} // namespace sk
