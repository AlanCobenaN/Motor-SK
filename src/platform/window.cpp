#include "window.h"

#include <windows.h>
#include <windowsx.h>

#include "../core/log.h"

namespace sk {

namespace {

const char* kWindowClassName = "MotorSKWindow";

} // namespace

Window::~Window() {
    destroy();
}

bool Window::create(int width, int height, const char* title) {
    instance_ = GetModuleHandleW(nullptr);

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&Window::wndProc);
    wc.hInstance = static_cast<HINSTANCE>(instance_);
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;

    // Registrar dos veces es inofensivo en una sola instancia del exe.
    RegisterClassExA(&wc);

    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    hwnd_ = CreateWindowExA(
        0,
        kWindowClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr,
        static_cast<HINSTANCE>(instance_),
        this);

    if (!hwnd_) {
        SK_ERROR("CreateWindowExA fallo (error %lu)", GetLastError());
        return false;
    }

    width_ = width;
    height_ = height;

    ShowWindow(static_cast<HWND>(hwnd_), SW_SHOW);
    UpdateWindow(static_cast<HWND>(hwnd_));

    SK_INFO("Ventana creada (%dx%d)", width, height);
    return true;
}

void Window::destroy() {
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
}

bool Window::pumpEvents() {
    MSG msg{};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            shouldClose_ = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return !shouldClose_;
}

bool Window::keyDown(int vk) const {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

Vec2 Window::consumeMouseDelta() {
    Vec2 d = pendingDelta_;
    pendingDelta_ = {};
    return d;
}

float Window::consumeWheel() {
    float w = pendingWheel_;
    pendingWheel_ = 0.0f;
    return w;
}

long __stdcall Window::wndProc(void* hwndPtr, unsigned int msg, unsigned long long wParam, long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    Window* self = reinterpret_cast<Window*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self) {
        switch (msg) {
            case WM_SIZE: {
                self->width_ = LOWORD(lParam);
                self->height_ = HIWORD(lParam);
                if (self->width_ > 0 && self->height_ > 0) self->resized_ = true;
                return 0;
            }
            case WM_RBUTTONDOWN: {
                self->rmbDown_ = true;
                self->tracking_ = false;
                SetCapture(hwnd);
                return 0;
            }
            case WM_RBUTTONUP: {
                self->rmbDown_ = false;
                self->tracking_ = false;
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            case WM_MOUSEMOVE: {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                if (self->rmbDown_) {
                    if (!self->tracking_) {
                        self->tracking_ = true;
                    } else {
                        self->pendingDelta_.x += static_cast<float>(x - self->lastX_);
                        self->pendingDelta_.y += static_cast<float>(y - self->lastY_);
                    }
                    self->lastX_ = x;
                    self->lastY_ = y;
                }
                return 0;
            }
            case WM_MOUSEWHEEL: {
                self->pendingWheel_ += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam));
                return 0;
            }
            case WM_DESTROY: {
                PostQuitMessage(0);
                return 0;
            }
            default:
                break;
        }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace sk
