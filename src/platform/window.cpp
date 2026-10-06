#include "window.h"

#include <windows.h>
#include <windowsx.h>

#include "../core/log.h"
#include "../ui/theme.h"

namespace sk {

namespace {

const char* kWindowClassName = "MotorSKWindow";

// Icono del programa (recurso 1 definido en app.rc a partir de logo.ico).
// cx/cy a 0 => tamano estandar (SM_CXICON); LR_SHARED: no hay que
// destruirlo (vive mientras vive el modulo).
HICON loadAppIcon(int cx, int cy) {
    UINT flags = LR_SHARED;
    if (cx == 0 || cy == 0) flags |= LR_DEFAULTSIZE;
    return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                         MAKEINTRESOURCEW(1), IMAGE_ICON,
                                         cx, cy, flags));
}

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
    wc.hIcon = loadAppIcon(0, 0);       // LR_DEFAULTSIZE: tamanos estandar
    wc.hIconSm = loadAppIcon(0, 0);
    wc.lpszClassName = kWindowClassName;

    // Registrar dos veces es inofensivo en una sola instancia del exe.
    RegisterClassExA(&wc);

    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    // WS_CLIPCHILDREN: el BitBlt del present de Vulkan no pinta sobre las
    // ventanas hijas (si no, la banda de pestañas queda tapada cada frame).
    hwnd_ = CreateWindowExA(
        0,
        kWindowClassName,
        title,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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

    // Icono explicito de la ventana (barra de tareas / Alt+Tab). Ojo:
    // no usar el nombre "small" (macro de rpcndr.h: #define small char).
    const HICON bigIcon = loadAppIcon(GetSystemMetrics(SM_CXICON),
                                      GetSystemMetrics(SM_CYICON));
    const HICON smallIcon = loadAppIcon(GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON));
    SendMessageW(static_cast<HWND>(hwnd_), WM_SETICON, ICON_BIG,
                 reinterpret_cast<LPARAM>(bigIcon));
    SendMessageW(static_cast<HWND>(hwnd_), WM_SETICON, ICON_SMALL,
                 reinterpret_cast<LPARAM>(smallIcon));

    // Barra de titulo oscura a juego con el tema de la interfaz.
    theme::enableDarkTitleBar(static_cast<HWND>(hwnd_));

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

bool Window::hasFocus() const {
    return GetForegroundWindow() == static_cast<HWND>(hwnd_);
}

bool Window::textInputFocused() const {
    HWND focus = GetFocus();
    if (!focus) return false;
    char cls[32]{};
    GetClassNameA(focus, cls, static_cast<int>(sizeof(cls)));
    return lstrcmpA(cls, "EDIT") == 0;
}

Vec2 Window::consumeMouseDelta() const {
    Vec2 d = pendingDelta_;
    pendingDelta_ = {};
    return d;
}

float Window::consumeWheel() const {
    float w = pendingWheel_;
    pendingWheel_ = 0.0f;
    return w;
}

std::optional<Vec2> Window::consumeLeftPressed() {
    std::optional<Vec2> p = pendingPress_;
    pendingPress_.reset();
    return p;
}

std::optional<Vec2> Window::consumeLeftReleased() {
    std::optional<Vec2> p = pendingRelease_;
    pendingRelease_.reset();
    return p;
}

long long __stdcall Window::wndProc(void* hwndPtr, unsigned int msg, unsigned long long wParam, long long lParam) {
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
            case WM_DPICHANGED: {
                // Cambio de escala del escritorio: redimensionar al rect
                // sugerido por Windows (llega como WM_SIZE y el renderer
                // y la UI se reajustan solos).
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case WM_LBUTTONDOWN: {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                self->lmbDown_ = true;
                self->mousePos_ = {static_cast<float>(x), static_cast<float>(y)};
                self->pendingPress_ = self->mousePos_;
                SetCapture(hwnd);
                return 0;
            }
            case WM_LBUTTONUP: {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                self->lmbDown_ = false;
                self->mousePos_ = {static_cast<float>(x), static_cast<float>(y)};
                self->pendingRelease_ = self->mousePos_;
                // El capture del click derecho manda: no soltarlo al
                // levantar el izquierdo (la cámara seguiría mirando).
                if (!self->rmbDown_ && GetCapture() == hwnd) ReleaseCapture();
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
                if (!self->lmbDown_ && GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            case WM_MOUSEMOVE: {
                const int x = GET_X_LPARAM(lParam);
                const int y = GET_Y_LPARAM(lParam);
                self->mousePos_ = {static_cast<float>(x), static_cast<float>(y)};
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
