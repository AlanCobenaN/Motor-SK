#include "workspace_tabs.h"

#include <windows.h>

#include "../core/log.h"
#include "theme.h"

namespace sk {

namespace {

const char* kTabsClass = "MotorSKWorkspaceTabs";

const char* kTabNames[] = {"PLACE", "CODE", "GUI"};

// Pinta un tab redondo al estilo del resto de la interfaz: fondo oscuro,
// texto blanco y bordes redondeados. La division activa va en acento.
void paintTab(const DRAWITEMSTRUCT& dis, bool active) {
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;

    COLORREF fill = active ? theme::accent() : theme::surface();
    if (!active && (dis.itemState & ODS_SELECTED)) fill = theme::surfacePressed();

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, active ? theme::accent() : theme::border());
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(brush);

    char label[64]{};
    GetWindowTextA(dis.hwndItem, label, static_cast<int>(sizeof(label)));

    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme::text());
    DrawTextA(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

} // namespace

bool WorkspaceTabs::create(void* parentHwnd) {
    parent_ = parentHwnd;

    static bool registered = false;
    if (!registered) {
        HINSTANCE inst = GetModuleHandleW(nullptr);

        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&WorkspaceTabs::wndProc);
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kTabsClass;
        RegisterClassExA(&wc);

        registered = true;
    }

    HWND parent = static_cast<HWND>(parent_);
    RECT rc{};
    GetClientRect(parent, &rc);
    HINSTANCE inst = GetModuleHandleW(nullptr);

    hwnd_ = CreateWindowExA(0, kTabsClass, "", WS_CHILD,
                            0, 0, rc.right, kToolbarHeight, parent, nullptr,
                            inst, this);
    if (!hwnd_) {
        SK_ERROR("WorkspaceTabs: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }

    for (int i = 0; i < kTabCount; ++i) {
        buttons_[i] = CreateWindowExA(0, "BUTTON", kTabNames[i],
                                      WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      12 + i * 118, 8, 110, 32,
                                      static_cast<HWND>(hwnd_),
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(i + 1)),
                                      inst, nullptr);
        SendMessageA(static_cast<HWND>(buttons_[i]), WM_SETFONT,
                     reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    }

    resize(rc.right, rc.bottom);
    SK_INFO("Divisiones del workspace listas (PLACE | CODE | GUI)");
    return true;
}

void WorkspaceTabs::destroy() {
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
        for (int i = 0; i < kTabCount; ++i) buttons_[i] = nullptr;
    }
}

void WorkspaceTabs::setVisible(bool visible) {
    visible_ = visible;
    if (hwnd_) ShowWindow(static_cast<HWND>(hwnd_), visible ? SW_SHOW : SW_HIDE);
}

void WorkspaceTabs::selectTab(int index) {
    if (index < 0 || index >= kTabCount || index == active_) return;
    active_ = index;
    for (int i = 0; i < kTabCount; ++i) {
        if (buttons_[i]) {
            InvalidateRect(static_cast<HWND>(buttons_[i]), nullptr, TRUE);
        }
    }
}

void WorkspaceTabs::resize(int width, int height) {
    if (!hwnd_) return;
    const int bandHeight = (height < kToolbarHeight) ? height : kToolbarHeight;
    MoveWindow(static_cast<HWND>(hwnd_), 0, 0, width, bandHeight, TRUE);

    const int margin = 12;
    for (int i = 0; i < kTabCount; ++i) {
        if (buttons_[i]) {
            MoveWindow(static_cast<HWND>(buttons_[i]), margin + i * 118, 8, 110, 32, TRUE);
        }
    }
}

long long __stdcall WorkspaceTabs::wndProc(void* hwndPtr, unsigned int msg,
                                            unsigned long long wParam,
                                            long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    WorkspaceTabs* self =
        reinterpret_cast<WorkspaceTabs*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<WorkspaceTabs*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self) {
        switch (msg) {
            case WM_DRAWITEM: {
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                const int index = static_cast<int>(dis->CtlID) - 1;
                if (index >= 0 && index < kTabCount) {
                    paintTab(*dis, index == self->active_);
                    return TRUE;
                }
                break;
            }
            case WM_COMMAND: {
                if (HIWORD(wParam) == BN_CLICKED) {
                    self->selectTab(static_cast<int>(LOWORD(wParam)) - 1);
                    return 0;
                }
                break;
            }
            default:
                break;
        }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace sk
