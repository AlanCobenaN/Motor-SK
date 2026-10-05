#include "script_picker.h"

#include <windows.h>

#include <vector>

#include "../project/project.h"
#include "../project/scripts.h"
#include "dark_button.h"
#include "theme.h"

namespace sk {
namespace ui {

namespace {

const char* kPickerClass = "MotorSKScriptPicker";

constexpr int kListId = 100;

struct PickerState {
    HWND list = nullptr;
    std::vector<std::string> rels; // sin "(ninguno)"
    std::string value;
    bool accepted = false;
    bool done = false;
};

PickerState* gPicker = nullptr;

// Rel del item seleccionado; "(ninguno)" (indice 0) => "".
std::string selectedRel(const PickerState& state) {
    if (!state.list) return "";
    const int index =
        static_cast<int>(SendMessageA(state.list, LB_GETCURSEL, 0, 0));
    if (index <= 0) return "";
    if (index - 1 >= static_cast<int>(state.rels.size())) return "";
    return state.rels[index - 1];
}

long long __stdcall pickerWndProc(void* hwndPtr, unsigned int msg,
                                  unsigned long long wParam, long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = GetModuleHandleW(nullptr);

            HWND label = CreateWindowExA(0, "STATIC", "Script asociado:",
                                         WS_CHILD | WS_VISIBLE,
                                         12, 12, 336, 20, hwnd, nullptr,
                                         inst, nullptr);
            HWND list = CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                            WS_VSCROLL | LBS_NOTIFY |
                                            LBS_NOINTEGRALHEIGHT,
                                        12, 38, 336, 216, hwnd,
                                        reinterpret_cast<HMENU>(
                                            static_cast<INT_PTR>(kListId)),
                                        inst, nullptr);
            HWND ok = CreateWindowExA(0, "BUTTON", "Aceptar",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                          BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                                      152, 268, 96, 30, hwnd,
                                      reinterpret_cast<HMENU>(
                                          static_cast<INT_PTR>(IDOK)),
                                      inst, nullptr);
            HWND cancel = CreateWindowExA(0, "BUTTON", "Cancelar",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                              BS_OWNERDRAW,
                                          256, 268, 96, 30, hwnd,
                                          reinterpret_cast<HMENU>(
                                              static_cast<INT_PTR>(IDCANCEL)),
                                          inst, nullptr);

            SendMessageA(label, WM_SETFONT,
                         reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
            SendMessageA(list, WM_SETFONT,
                         reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
            makeDarkButton(ok);
            makeDarkButton(cancel);

            if (gPicker) {
                gPicker->list = list;
                SendMessageA(list, LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>("(ninguno)"));
                for (const std::string& rel : gPicker->rels) {
                    SendMessageA(list, LB_ADDSTRING, 0,
                                 reinterpret_cast<LPARAM>(rel.c_str()));
                }
                int initial = 0; // "(ninguno)"
                for (size_t i = 0; i < gPicker->rels.size(); ++i) {
                    if (gPicker->rels[i] == gPicker->value) {
                        initial = static_cast<int>(i) + 1;
                        break;
                    }
                }
                SendMessageA(list, LB_SETCURSEL, initial, 0);
            }
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
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, theme::text());
            SetBkColor(hdc, theme::listBackground());
            return reinterpret_cast<LRESULT>(theme::listBackgroundBrush());
        }
        case WM_COMMAND: {
            const int id = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == kListId && code == LBN_DBLCLK && gPicker) {
                gPicker->value = selectedRel(*gPicker);
                gPicker->accepted = true;
                DestroyWindow(hwnd);
            } else if (id == IDOK && gPicker) {
                gPicker->value = selectedRel(*gPicker);
                gPicker->accepted = true;
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
            if (gPicker) gPicker->done = true;
            return 0;
        default:
            break;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

} // namespace

bool pickScript(HWND owner, const Project& project,
                const std::string& initial, std::string& outRel) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&pickerWndProc);
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kPickerClass;
        RegisterClassExA(&wc);
        registered = true;
    }

    PickerState state;
    state.rels = scripts::allScriptRels(project);
    state.value = initial;
    gPicker = &state;

    RECT rect{0, 0, 364, 314};
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRect(&rect, style, FALSE);

    // Centrar sobre la ventana padre.
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    const int x = ownerRect.left +
                  ((ownerRect.right - ownerRect.left) - (rect.right - rect.left)) / 2;
    const int y = ownerRect.top +
                  ((ownerRect.bottom - ownerRect.top) - (rect.bottom - rect.top)) / 2;

    HWND hwnd = CreateWindowExA(WS_EX_TOOLWINDOW, kPickerClass, "Asociar script",
                                style | WS_VISIBLE, x, y,
                                rect.right - rect.left, rect.bottom - rect.top,
                                owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        gPicker = nullptr;
        return false;
    }
    theme::enableDarkTitleBar(hwnd);
    SetFocus(state.list);

    EnableWindow(owner, FALSE);

    MSG msg{};
    while (!state.done && GetMessageA(&msg, nullptr, 0, 0) > 0) {
        if (IsDialogMessageA(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    gPicker = nullptr;

    if (!state.accepted) return false;
    outRel = state.value;
    return true;
}

} // namespace ui
} // namespace sk
