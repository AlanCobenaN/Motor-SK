#pragma once

#include "../math/math.h"

namespace sk {

// Ventana Win32 con estado de input para la cámara libre.
// El teclado (WASD/shift) se consulta con GetAsyncKeyState; el ratón
// (click derecho, rueda, deltas de movimiento) llega por mensajes.
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

    // Delta acumulado desde la última consulta y rueda acumulada.
    Vec2 consumeMouseDelta();
    float consumeWheel();

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
    static long __stdcall wndProc(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam);

    void* hwnd_ = nullptr;
    void* instance_ = nullptr;

    bool shouldClose_ = false;
    bool rmbDown_ = false;
    bool resized_ = false;

    bool tracking_ = false;
    Vec2 pendingDelta_;
    float pendingWheel_ = 0.0f;
    int lastX_ = 0;
    int lastY_ = 0;

    int width_ = 0;
    int height_ = 0;
};

} // namespace sk
