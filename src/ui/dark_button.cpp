#include "dark_button.h"

#include <list>

#include "theme.h"

namespace sk {
namespace ui {

namespace {

// std::list: los punteros a los elementos son estables (el subclass proc
// guarda un DarkButton* en dwRefData).
std::list<DarkButton> gButtons;

LRESULT CALLBACK darkButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                UINT_PTR /*subclassId*/, DWORD_PTR refData) {
    auto* button = reinterpret_cast<DarkButton*>(refData);
    switch (msg) {
        case WM_MOUSEMOVE: {
            const bool disabled = (GetWindowLongA(hwnd, GWL_STYLE) & WS_DISABLED) != 0;
            if (!disabled && !button->hover) {
                button->hover = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            break;
        }
        case WM_MOUSELEAVE:
            if (button->hover) {
                button->hover = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            break;
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, darkButtonProc, 1);
            gButtons.remove_if([=](const DarkButton& b) { return b.hwnd == hwnd; });
            break;
        default:
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

void makeDarkButton(HWND hwnd) {
    gButtons.push_back({hwnd, false});
    SetWindowSubclass(hwnd, darkButtonProc, 1,
                      reinterpret_cast<DWORD_PTR>(&gButtons.back()));
}

DarkButton* findDarkButton(HWND hwnd) {
    for (DarkButton& b : gButtons) {
        if (b.hwnd == hwnd) return &b;
    }
    return nullptr;
}

bool paintDarkButton(const DRAWITEMSTRUCT& dis, bool drawText) {
    if (dis.CtlType != ODT_BUTTON) return false;
    const DarkButton* b = findDarkButton(dis.hwndItem);
    if (!b) return false;
    theme::paintDarkButton(dis, b->hover, drawText);
    return true;
}

} // namespace ui
} // namespace sk
