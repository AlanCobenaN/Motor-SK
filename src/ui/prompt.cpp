#include "prompt.h"

#include <windows.h>

#include "dark_button.h"
#include "theme.h"

namespace sk {
namespace ui {

namespace {

const char* kPromptClass = "MotorSKPrompt";

struct PromptState {
    HWND edit = nullptr;
    std::string value;
    bool accepted = false;
    bool done = false;
};

PromptState* gPrompt = nullptr;

long long __stdcall promptWndProc(void* hwndPtr, unsigned int msg,
                                  unsigned long long wParam, long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = GetModuleHandleW(nullptr);

            HWND label = CreateWindowExA(0, "STATIC", "Nombre:",
                                         WS_CHILD | WS_VISIBLE,
                                         12, 14, 326, 20, hwnd, nullptr, inst, nullptr);
            HWND edit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                        12, 40, 326, 30, hwnd, nullptr, inst, nullptr);
            HWND ok = CreateWindowExA(0, "BUTTON", "Aceptar",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                          BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                                      154, 86, 92, 30, hwnd,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)),
                                      inst, nullptr);
            HWND cancel = CreateWindowExA(0, "BUTTON", "Cancelar",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                          254, 86, 92, 30, hwnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)),
                                          inst, nullptr);

            for (HWND c : {label, edit}) {
                SendMessageA(c, WM_SETFONT, reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
            }
            makeDarkButton(ok);
            makeDarkButton(cancel);
            if (gPrompt) gPrompt->edit = edit;
            return 0;
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (paintDarkButton(*dis)) return TRUE;
            break;
        }
        case WM_CTLCOLORSTATIC: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, theme::text());
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<LRESULT>(theme::backgroundBrush());
        }
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, theme::text());
            SetBkColor(hdc, theme::surface());
            return reinterpret_cast<LRESULT>(theme::surfaceBrush());
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            if (id == IDOK && gPrompt) {
                char buf[512]{};
                GetWindowTextA(gPrompt->edit, buf, static_cast<int>(sizeof(buf)));
                gPrompt->value = buf;
                gPrompt->accepted = true;
                DestroyWindow(hwnd);
            } else if (id == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (gPrompt) gPrompt->done = true;
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace

std::string promptText(HWND owner, const char* title, const char* initial) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&promptWndProc);
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kPromptClass;
        RegisterClassExA(&wc);
        registered = true;
    }

    PromptState state;
    gPrompt = &state;

    RECT rect{0, 0, 358, 130};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&rect, style, FALSE);

    // Centrar sobre la ventana padre.
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - (rect.right - rect.left)) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - (rect.bottom - rect.top)) / 2;

    HWND hwnd = CreateWindowExA(WS_EX_TOOLWINDOW, kPromptClass, title, style | WS_VISIBLE,
                                x, y, rect.right - rect.left, rect.bottom - rect.top,
                                owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        gPrompt = nullptr;
        return "";
    }
    theme::enableDarkTitleBar(hwnd);

    SetWindowTextA(state.edit, initial);
    SetFocus(state.edit);
    SendMessageA(state.edit, EM_SETSEL, 0, -1);

    EnableWindow(owner, FALSE);

    MSG msg{};
    while (!state.done && GetMessageA(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageA(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    gPrompt = nullptr;
    return state.accepted ? state.value : "";
}

} // namespace ui
} // namespace sk
