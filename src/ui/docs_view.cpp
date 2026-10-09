#include "docs_view.h"
#include <commctrl.h>
#include <cstddef>
#include <cstdio>
#include <string>
#include "theme.h"

namespace sk {
namespace {
const int kSplit = 300;
const int kMargin = 8;
} // namespace

LRESULT CALLBACK DocsView::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DocsView* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<DocsView*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<DocsView*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProc(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

} // namespace sk
