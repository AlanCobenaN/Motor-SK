#include "workspace_tabs.h"

#include <windows.h>

#include "../core/log.h"
#include "dark_button.h"
#include "theme.h"

namespace sk {

namespace {

const char* kTabsClass = "MotorSKWorkspaceTabs";

const char* kTabNames[] = {"PLACE", "CODE", "GUI"};

// Fila de atajos: herramientas del viewport y el boton Part. Medidas fijas
// para que las pruebas puedan localizarlos por texto/posicion.
struct ToolButton {
    int id;
    const char* text;
    int x;
    int width;
};
const ToolButton kToolButtons[] = {
    {110, "Seleccionar", 10, 110},
    {111, "Mover", 126, 76},
    {112, "Escalar", 208, 86},
    {113, "Rotar", 300, 76},
};
constexpr int kPartX = 382;
constexpr int kPartWidth = 74;
constexpr int kRowY = 5;
constexpr int kRowHeight = 26;

// Icono GDI de cada herramienta, en un cuadro de 14x14 centrado en la
// fila; lineas simples en color texto sobre el fondo (oscuro o acento).
void paintToolIcon(HDC hdc, const RECT& rc, int tool) {
    const int cx = rc.left + 14;
    const int cy = rc.top + (rc.bottom - rc.top) / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, theme::text());
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    auto line = [&](int x0, int y0, int x1, int y1) {
        MoveToEx(hdc, cx + x0, cy + y0, nullptr);
        LineTo(hdc, cx + x1, cy + y1);
    };
    switch (tool) {
        case 0: // cursor de seleccion
            line(-4, -6, -4, 6);
            line(-4, -6, 4, 0);
            line(-4, 6, 4, 0);
            break;
        case 1: // mover: cruz con puntas de flecha
            line(-6, 0, 6, 0);
            line(0, -6, 0, 6);
            line(-6, 0, -3, -3);
            line(-6, 0, -3, 3);
            line(6, 0, 3, -3);
            line(6, 0, 3, 3);
            line(0, -6, -3, -3);
            line(0, -6, 3, -3);
            line(0, 6, -3, 3);
            line(0, 6, 3, 3);
            break;
        case 2: // escalar: dos esquinas opuestas
            line(-6, -6, -1, -6);
            line(-6, -6, -6, -1);
            line(6, 6, 1, 6);
            line(6, 6, 6, 1);
            line(-4, -4, 4, 4);
            break;
        default: // rotar: arco con punta
            line(-5, 3, -3, -4);
            line(-3, -4, 4, -4);
            line(4, -4, 5, 2);
            line(5, 2, 1, 0);
            break;
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

// Herramienta de la fila de atajos: icono + texto; la activa va en acento.
void paintTool(const DRAWITEMSTRUCT& dis, bool active) {
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;

    if (active) {
        HBRUSH brush = CreateSolidBrush(theme::accent());
        HPEN pen = CreatePen(PS_SOLID, 1, theme::accent());
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        DeleteObject(brush);
    } else if (!ui::paintDarkButton(dis, false)) {
        // Solo la base: la etiqueta se pinta abajo (si paintDarkButton
        // dibujara el texto centrado saldria repetido junto al icono).
        return;
    }

    paintToolIcon(hdc, rc, dis.CtlID - 110);

    char label[64]{};
    GetWindowTextA(dis.hwndItem, label, static_cast<int>(sizeof(label)));
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme::text());
    RECT textRc = rc;
    textRc.left += 26;
    DrawTextA(hdc, label, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

// Pinta una pestana compacta de la navbar: fondo oscuro, texto blanco y
// bordes redondeados. La division activa va en acento.
void paintTab(const DRAWITEMSTRUCT& dis, bool active) {
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;

    COLORREF fill = active ? theme::accent() : theme::surface();
    if (!active && (dis.itemState & ODS_SELECTED)) fill = theme::surfacePressed();

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, active ? theme::accent() : theme::border());
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
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

// Boton "Part" de la barra de atajos: base de boton oscuro (con hover,
// via dark_button) y un cubito de acento a la izquierda de la etiqueta.
void paintPart(const DRAWITEMSTRUCT& dis) {
    if (!ui::paintDarkButton(dis)) return;

    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));

    SIZE sz{};
    GetTextExtentPoint32A(hdc, "Part", 4, &sz);
    const int textX = rc.left + ((rc.right - rc.left) - static_cast<int>(sz.cx)) / 2;
    const int iconX = textX - 18;
    const int iconY = rc.top + ((rc.bottom - rc.top) - 12) / 2;

    if (iconX > rc.left + 2) {
        HBRUSH brush = CreateSolidBrush(theme::accent());
        HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
        RoundRect(hdc, iconX, iconY, iconX + 12, iconY + 12, 3, 3);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
        DeleteObject(brush);
    }

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
                            0, 0, rc.right, kTopBandHeight, parent, nullptr,
                            inst, this);
    if (!hwnd_) {
        SK_ERROR("WorkspaceTabs: CreateWindowEx fallo (error %lu)", GetLastError());
        return false;
    }

    // Fila de atajos (arriba): herramientas del viewport + "Part".
    for (int i = 0; i < kToolCount; ++i) {
        const ToolButton& def = kToolButtons[i];
        toolButtons_[i] = CreateWindowExA(
            0, "BUTTON", def.text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            def.x, kRowY, def.width, kRowHeight,
            static_cast<HWND>(hwnd_),
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(def.id)),
            inst, nullptr);
        SendMessageA(static_cast<HWND>(toolButtons_[i]), WM_SETFONT,
                     reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
        ui::makeDarkButton(static_cast<HWND>(toolButtons_[i]));
    }
    partButton_ = CreateWindowExA(0, "BUTTON", "Part",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                  kPartX, kRowY, kPartWidth, kRowHeight,
                                  static_cast<HWND>(hwnd_),
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPart)),
                                  inst, nullptr);
    SendMessageA(static_cast<HWND>(partButton_), WM_SETFONT,
                 reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    ui::makeDarkButton(static_cast<HWND>(partButton_));

    // Navbar (abajo): pestanas compactas.
    for (int i = 0; i < kTabCount; ++i) {
        buttons_[i] = CreateWindowExA(0, "BUTTON", kTabNames[i],
                                      WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                      10 + i * 82, kShortcutHeight + 6, 76, 24,
                                      static_cast<HWND>(hwnd_),
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(i + 1)),
                                      inst, nullptr);
        SendMessageA(static_cast<HWND>(buttons_[i]), WM_SETFONT,
                     reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    }

    resize(rc.right, rc.bottom);
    SK_INFO("Banda superior lista (atajos + navbar PLACE | CODE | GUI)");
    return true;
}

void WorkspaceTabs::destroy() {
    if (hwnd_) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
        for (int i = 0; i < kTabCount; ++i) buttons_[i] = nullptr;
        for (int i = 0; i < kToolCount; ++i) toolButtons_[i] = nullptr;
        partButton_ = nullptr;
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
    if (onTabChanged_) onTabChanged_(index);
}

void WorkspaceTabs::setTool(int tool) {
    if (tool < 0 || tool >= kToolCount || tool == tool_) return;
    tool_ = tool;
    for (int i = 0; i < kToolCount; ++i) {
        if (toolButtons_[i]) {
            InvalidateRect(static_cast<HWND>(toolButtons_[i]), nullptr, TRUE);
        }
    }
    if (onToolChanged_) onToolChanged_(tool);
}

void WorkspaceTabs::resize(int width, int height) {
    if (!hwnd_) return;
    const int bandHeight = (height < kTopBandHeight) ? height : kTopBandHeight;
    MoveWindow(static_cast<HWND>(hwnd_), 0, 0, width, bandHeight, TRUE);

    for (int i = 0; i < kToolCount; ++i) {
        if (toolButtons_[i]) {
            MoveWindow(static_cast<HWND>(toolButtons_[i]),
                       kToolButtons[i].x, kRowY,
                       kToolButtons[i].width, kRowHeight, TRUE);
        }
    }
    if (partButton_) {
        MoveWindow(static_cast<HWND>(partButton_), kPartX, kRowY,
                   kPartWidth, kRowHeight, TRUE);
    }
    for (int i = 0; i < kTabCount; ++i) {
        if (buttons_[i]) {
            MoveWindow(static_cast<HWND>(buttons_[i]),
                       10 + i * 82, kShortcutHeight + 6, 76, 24, TRUE);
        }
    }
}

long long __stdcall WorkspaceTabs::wndProc(void* hwndPtr, unsigned int msg,
                                           unsigned long long wParam, long long lParam) {
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
                if (dis->CtlID >= kIdToolBase &&
                    dis->CtlID < kIdToolBase + kToolCount) {
                    paintTool(*dis,
                              static_cast<int>(dis->CtlID - kIdToolBase) ==
                                  self->tool_);
                    return TRUE;
                }
                if (dis->CtlID == kIdPart) {
                    paintPart(*dis);
                    return TRUE;
                }
                const int index = static_cast<int>(dis->CtlID) - 1;
                if (index >= 0 && index < kTabCount) {
                    paintTab(*dis, index == self->active_);
                    return TRUE;
                }
                break;
            }
            case WM_COMMAND: {
                const int id = static_cast<int>(LOWORD(wParam));
                if (HIWORD(wParam) == BN_CLICKED &&
                    id >= kIdToolBase && id < kIdToolBase + kToolCount) {
                    self->setTool(id - kIdToolBase);
                    return 0;
                }
                if (id == kIdPart) {
                    if (self->onAddPart_) self->onAddPart_();
                    return 0;
                }
                if (HIWORD(wParam) == BN_CLICKED && id >= 1 && id <= kTabCount) {
                    self->selectTab(id - 1);
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
