#include "workspace_tabs.h"

#include <windows.h>
#include <commctrl.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../core/log.h"
#include "dark_button.h"
#include "theme.h"

namespace sk {

namespace {

const char* kTabsClass = "MotorSKWorkspaceTabs";

const char* kTabNames[] = {"PLACE", "CODE", "GUI"};

// Fila de atajos: herramientas del viewport como tarjetas compactas
// (icono arriba, nombre abajo) y el boton Part. Medidas fijas para que
// las pruebas puedan localizarlos por texto/posicion.
struct ToolButton {
    int id;
    const char* text;
    int x;
    int width;
};
const ToolButton kToolButtons[] = {
    {110, "Select", 10, 52},
    {111, "Move", 70, 52},
    {112, "Scale", 130, 52},
    {113, "Rotate", 190, 52},
};
// El boton "Part" es una tarjeta igualada a las demas (mismo alto y
// ancho, icono arriba y nombre abajo).
constexpr int kPartX = 250;
constexpr int kPartWidth = 52;
constexpr int kRowY = 2;
constexpr int kRowHeight = 52;
constexpr int kPartY = 2;
constexpr int kPartHeight = 52;
// Boton delgado con triangulo hacia abajo, a la derecha del boton Part,
// con la misma altura que las tarjetas.
constexpr int kPartMenuWidth = 18;
constexpr int kPartMenuX = kPartX + kPartWidth + 3;

// Rallita divisoria entre Rotate (termina en x=242) y Cube (x=250).
constexpr int kDividerX = 244;
constexpr int kDividerW = 6;

// Dos cajitas de pasos despues del triangulo del boton Part: la de
// arriba es el paso de mover/escalar/arrastrar (flechas, "cm"/"m") y la
// de abajo el paso de rotacion (logo de rotar, grados enteros con "\xb0").
// Cada cajita tiene un STATIC owner-draw de fondo (caja + icono) y un
// EDIT encima a la derecha del icono.
constexpr int kFieldX = 332;
constexpr int kFieldW = 108;
constexpr int kFieldH = 22;
constexpr int kFieldYTop = 4;
constexpr int kFieldYBot = 30;
constexpr int kFieldIconW = 24;   // zona del icono a la izquierda
constexpr int kFieldEditDX = kFieldIconW + 1;
constexpr int kFieldEditW = kFieldW - kFieldEditDX - 3;
constexpr int kFieldEditH = kFieldH - 4;

// Desplegable de formas del boton Part: triangulito dentro de la tarjeta
// y ventana emergente (popup) con las variantes.
struct ShapeOption {
    int shape;         // indice de sk::Shape
    const char* label; // nombre mostrado
};
const ShapeOption kShapeOptions[] = {
    {0, "Cube"},    {3, "Cylinder"}, {2, "Sphere"},     {1, "Rhombus"},
    {4, "Wedge"},   {5, "CornerWedge"}, {6, "Capsule"},
};
constexpr int kShapeOptionCount = 7;
constexpr int kMenuWidth = 128;
constexpr int kMenuRowHeight = 24;
const char* kShapeMenuClass = "MotorSKShapeMenu";

// Icono GDI de cada herramienta, centrado en la mitad superior de la
// tarjeta; lineas simples en color texto sobre el fondo (oscuro o
// acento). Trazos proporcionales a la tarjeta de 52px de alto.
void paintToolIcon(HDC hdc, const RECT& rc, int tool) {
    const int cx = (rc.left + rc.right) / 2;
    const int cy = rc.top + 12;
    HPEN pen = CreatePen(PS_SOLID, 1, theme::text());
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    auto line = [&](int x0, int y0, int x1, int y1) {
        MoveToEx(hdc, cx + x0, cy + y0, nullptr);
        LineTo(hdc, cx + x1, cy + y1);
    };
    switch (tool) {
        case 0: // cursor de seleccion
            line(-4, -7, -4, 7);
            line(-4, -7, 5, 0);
            line(-4, 7, 5, 0);
            break;
        case 1: // mover: cruz con puntas de flecha
            line(-7, 0, 7, 0);
            line(0, -7, 0, 7);
            line(-7, 0, -4, -3);
            line(-7, 0, -4, 3);
            line(7, 0, 4, -3);
            line(7, 0, 4, 3);
            line(0, -7, -3, -4);
            line(0, -7, 3, -4);
            line(0, 7, -3, 4);
            line(0, 7, 3, 4);
            break;
        case 2: // escalar: dos esquinas opuestas
            line(-7, -7, -2, -7);
            line(-7, -7, -7, -2);
            line(7, 7, 2, 7);
            line(7, 7, 7, 2);
            line(-4, -4, 4, 4);
            break;
        default: // rotar: arco con punta
            line(-6, 4, -3, -5);
            line(-3, -5, 5, -5);
            line(5, -5, 6, 3);
            line(6, 3, 2, 0);
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
    textRc.top += 19;
    DrawTextA(hdc, label, -1, &textRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
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

// Boton "Part" de la barra de atajos: igual que las tarjetas de
// herramienta (base oscura con hover, cubito de acento arriba y nombre
// abajo).
void paintPart(const DRAWITEMSTRUCT& dis) {
    if (!ui::paintDarkButton(dis, false)) return;

    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    const int cx = (rc.left + rc.right) / 2;
    const int cy = rc.top + 12;

    HBRUSH brush = CreateSolidBrush(theme::accent());
    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, cx - 7, cy - 7, cx + 7, cy + 7, 4, 4);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(brush);

    char label[64]{};
    GetWindowTextA(dis.hwndItem, label, static_cast<int>(sizeof(label)));
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme::text());
    RECT textRc = rc;
    textRc.top += 19;
    DrawTextA(hdc, label, -1, &textRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(hdc, oldFont);
}

// Triangulito dentro del boton Part: base oscura + flecha hacia abajo.
void paintPartMenu(const DRAWITEMSTRUCT& dis) {
    if (!ui::paintDarkButton(dis, false)) return;
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    const int cx = (rc.left + rc.right) / 2;
    const int cy = (rc.top + rc.bottom) / 2;
    POINT poly[3] = {{cx - 5, cy - 3}, {cx + 5, cy - 3}, {cx, cy + 4}};
    HBRUSH brush = CreateSolidBrush(theme::text());
    HPEN pen = CreatePen(PS_SOLID, 1, theme::text());
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    Polygon(hdc, poly, 3);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(brush);
}

// Opcion del desplegable de formas: fila oscura (acento si es la actual).
void paintShapeOption(const DRAWITEMSTRUCT& dis, bool selected) {
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    HBRUSH fill = CreateSolidBrush(selected ? theme::accent() : theme::surface());
    FillRect(hdc, &rc, fill);
    DeleteObject(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    MoveToEx(hdc, rc.left, rc.bottom - 1, nullptr);
    LineTo(hdc, rc.right, rc.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    char label[64]{};
    GetWindowTextA(dis.hwndItem, label, static_cast<int>(sizeof(label)));
    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, theme::uiFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, theme::text());
    RECT tr = rc;
    tr.left += 10;
    DrawTextA(hdc, label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

// Rallita divisoria: una linea vertical simple entre Rotate y Cube,
// sobre el fondo de la banda (para que no haya resto de pincel).
void paintDivider(const DRAWITEMSTRUCT& dis) {
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    HBRUSH fill = CreateSolidBrush(theme::background());
    FillRect(hdc, &rc, fill);
    DeleteObject(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    const int x = (rc.left + rc.right) / 2;
    MoveToEx(hdc, x, rc.top + 4, nullptr);
    LineTo(hdc, x, rc.bottom - 4);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

// Icono de cada cajita de pasos, en la zona izquierda: flechas de mover
// (cruz con puntas) arriba, arco de rotar abajo.
void paintStepIcon(HDC hdc, int cx, int cy, bool moveField) {
    HPEN pen = CreatePen(PS_SOLID, 1, theme::text());
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    auto line = [&](int x0, int y0, int x1, int y1) {
        MoveToEx(hdc, cx + x0, cy + y0, nullptr);
        LineTo(hdc, cx + x1, cy + y1);
    };
    if (moveField) {
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
    } else {
        line(-5, 3, -2, -4);
        line(-2, -4, 4, -4);
        line(4, -4, 5, 2);
        line(5, 2, 2, 0);
    }
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

// Cajita de pasos: fondo oscuro con borde redondeado y el icono a la
// izquierda; el EDIT de la derecha se pinta encima (WM_CTLCOLOREDIT le
// devuelve el mismo fondo, asi parece una cajita sola).
void paintStepBox(const DRAWITEMSTRUCT& dis, bool moveField) {
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;
    HBRUSH fill = CreateSolidBrush(theme::boxBackground());
    HPEN pen = CreatePen(PS_SOLID, 1, theme::border());
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, fill));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(fill);
    paintStepIcon(hdc, rc.left + kFieldIconW / 2, (rc.top + rc.bottom) / 2,
                  moveField);
}

// "1.43m" si vale >= 1, "0.52cm" si vale < 1 (los centimetros/metros
// del juego: 1 unidad de mundo = 1 cm aqui, solo cambia el sufijo).
std::string formatMove(float v) {
    char buf[32]{};
    if (v < 1.0f) {
        std::snprintf(buf, sizeof(buf), "%.2fcm", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2fm", v);
    }
    return buf;
}

// Grados enteros con el simbolo de grado pegado: "15\xb0".
std::string formatRot(int deg) {
    char buf[32]{};
    std::snprintf(buf, sizeof(buf), "%d\xb0", deg);
    return buf;
}

// Lee el numero de la cajita de mover (ignora sufijos/letras y el segundo
// punto). Devuelve -1 si el texto no tiene ningun digito.
float parseMove(const char* s) {
    char digits[64]{};
    int n = 0;
    bool dot = false;
    for (const char* p = s; *p && n < 63; ++p) {
        if (*p >= '0' && *p <= '9') {
            digits[n++] = *p;
        } else if (*p == '.' && !dot) {
            digits[n++] = '.';
            dot = true;
        }
    }
    if (n == 0) return -1.0f;
    return static_cast<float>(std::atof(digits));
}

// Lee el entero de la cajita de rotacion. Devuelve 0 si no hay digitos.
int parseRot(const char* s) {
    const char* p = s;
    while (*p && !(*p >= '0' && *p <= '9')) ++p;
    int v = 0;
    for (; *p >= '0' && *p <= '9'; ++p) v = v * 10 + (*p - '0');
    return v;
}

// Enter/Escape aplican el valor (mismo camino que EN_KILLFOCUS) y se
// tragan el "ding" del EDIT; los caracteres no validos se descartan:
// solo digitos (y un punto en la cajita de mover) para no ensuciar la
// cajita antes de commitear el sufijo.
LRESULT CALLBACK stepEditProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam, UINT_PTR uId, DWORD_PTR dwRef) {
    (void)dwRef;
    if (msg == WM_CHAR) {
        if (wParam == '\r') {
            SendMessageA(GetParent(hwnd), WM_COMMAND,
                         MAKEWPARAM(GetDlgCtrlID(hwnd), EN_KILLFOCUS),
                         reinterpret_cast<LPARAM>(hwnd));
            return 0;
        }
        if (wParam == 8) return DefSubclassProc(hwnd, msg, wParam, lParam);
        const bool moveField = (uId == 1001);
        if (wParam >= '0' && wParam <= '9') {
            return DefSubclassProc(hwnd, msg, wParam, lParam);
        }
        if (moveField && wParam == '.') {
            char buf[64]{};
            GetWindowTextA(hwnd, buf, static_cast<int>(sizeof(buf)));
            if (!std::strchr(buf, '.')) {
                return DefSubclassProc(hwnd, msg, wParam, lParam);
            }
            return 0;
        }
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        SendMessageA(GetParent(hwnd), WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), EN_KILLFOCUS),
                     reinterpret_cast<LPARAM>(hwnd));
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
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
    partButton_ = CreateWindowExA(0, "BUTTON", "Cube",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                  kPartX, kPartY, kPartWidth, kPartHeight,
                                  static_cast<HWND>(hwnd_),
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPart)),
                                  inst, nullptr);
    SendMessageA(static_cast<HWND>(partButton_), WM_SETFONT,
                 reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    ui::makeDarkButton(static_cast<HWND>(partButton_));

    // Triangulito dentro del boton Part: despliega las variantes.
    partMenuButton_ = CreateWindowExA(
        0, "BUTTON", "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        kPartMenuX, kPartY, kPartMenuWidth, kPartHeight,
        static_cast<HWND>(hwnd_),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPartMenu)), inst,
        nullptr);
    SendMessageA(static_cast<HWND>(partMenuButton_), WM_SETFONT,
                 reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
    ui::makeDarkButton(static_cast<HWND>(partMenuButton_));

    // Rallita divisoria entre Rotate y Cube.
    divider_ = CreateWindowExA(
        0, "STATIC", "",
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        kDividerX, kRowY, kDividerW, kRowHeight,
        static_cast<HWND>(hwnd_),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDivider)),
        inst, nullptr);

    // Cajitas de pasos: STATIC owner-draw de fondo (caja + icono) con un
    // EDIT encima a la derecha. Se crean primero las cajas y luego los
    // EDITs para que estos queden arriba en el z-order y reciban el click.
    for (int i = 0; i < 2; ++i) {
        stepBoxes_[i] = CreateWindowExA(
            0, "STATIC", "",
            WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            kFieldX, (i == 0) ? kFieldYTop : kFieldYBot, kFieldW, kFieldH,
            static_cast<HWND>(hwnd_),
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                (i == 0) ? kIdStepBoxMove : kIdStepBoxRot)),
            inst, nullptr);
    }
    stepEdits_[0] = CreateWindowExA(
        0, "EDIT", formatMove(moveStep_).c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        kFieldX + kFieldEditDX, kFieldYTop + 2, kFieldEditW, kFieldEditH,
        static_cast<HWND>(hwnd_),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStepEditMove)),
        inst, nullptr);
    stepEdits_[1] = CreateWindowExA(
        0, "EDIT", formatRot(rotateStep_).c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        kFieldX + kFieldEditDX, kFieldYBot + 2, kFieldEditW, kFieldEditH,
        static_cast<HWND>(hwnd_),
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStepEditRot)),
        inst, nullptr);
    for (int i = 0; i < 2; ++i) {
        if (!stepEdits_[i]) continue;
        SendMessageA(static_cast<HWND>(stepEdits_[i]), WM_SETFONT,
                     reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
        SetWindowSubclass(static_cast<HWND>(stepEdits_[i]), stepEditProc,
                          i == 0 ? 1001 : 1002, 0);
    }

    // Ventana emergente de formas (WS_POPUP para no quedar recortada por
    // la banda de 86px).
    static bool menuRegistered = false;
    if (!menuRegistered) {
        WNDCLASSEXA wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc =
            reinterpret_cast<WNDPROC>(&WorkspaceTabs::popupProc);
        wc.hInstance = inst;
        wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
        wc.hbrBackground = theme::backgroundBrush();
        wc.lpszClassName = kShapeMenuClass;
        RegisterClassExA(&wc);
        menuRegistered = true;
    }
    shapeMenu_ = CreateWindowExA(
        0, kShapeMenuClass, "", WS_POPUP | WS_BORDER, 0, 0, kMenuWidth,
        kShapeOptionCount * kMenuRowHeight, parent, nullptr, inst, this);
    if (shapeMenu_) {
        for (int i = 0; i < kShapeOptionCount; ++i) {
            shapeButtons_[i] = CreateWindowExA(
                0, "BUTTON", kShapeOptions[i].label,
                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, i * kMenuRowHeight,
                kMenuWidth, kMenuRowHeight, static_cast<HWND>(shapeMenu_),
                reinterpret_cast<HMENU>(
                    static_cast<INT_PTR>(kIdShapeBase + i)),
                inst, nullptr);
            SendMessageA(static_cast<HWND>(shapeButtons_[i]), WM_SETFONT,
                         reinterpret_cast<WPARAM>(theme::uiFont()), TRUE);
            ui::makeDarkButton(static_cast<HWND>(shapeButtons_[i]));
        }
        ShowWindow(static_cast<HWND>(shapeMenu_), SW_HIDE);
    }

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

    // Los controles hijos se apilan de abajo hacia arriba: el triangulito
    // (creado antes que la navbar) queda debajo del boton Part. Lo subimos
    // al frente para que reciba el clic.
    if (partMenuButton_) {
        SetWindowPos(static_cast<HWND>(partMenuButton_), HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
        for (int i = 0; i < kShapeOptionCount; ++i) shapeButtons_[i] = nullptr;
        partButton_ = nullptr;
        partMenuButton_ = nullptr;
        shapeMenu_ = nullptr;
        divider_ = nullptr;
        for (int i = 0; i < 2; ++i) {
            stepBoxes_[i] = nullptr;
            stepEdits_[i] = nullptr;
        }
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
        MoveWindow(static_cast<HWND>(partButton_), kPartX, kPartY,
                   kPartWidth, kPartHeight, TRUE);
    }
    if (partMenuButton_) {
        MoveWindow(static_cast<HWND>(partMenuButton_), kPartMenuX, kPartY,
                   kPartMenuWidth, kPartHeight, TRUE);
    }
    if (divider_) {
        MoveWindow(static_cast<HWND>(divider_), kDividerX, kRowY, kDividerW,
                   kRowHeight, TRUE);
    }
    for (int i = 0; i < 2; ++i) {
        const int fy = (i == 0) ? kFieldYTop : kFieldYBot;
        if (stepBoxes_[i]) {
            MoveWindow(static_cast<HWND>(stepBoxes_[i]), kFieldX, fy,
                       kFieldW, kFieldH, TRUE);
        }
        if (stepEdits_[i]) {
            MoveWindow(static_cast<HWND>(stepEdits_[i]), kFieldX + kFieldEditDX,
                       fy + 2, kFieldEditW, kFieldEditH, TRUE);
        }
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
                if (dis->CtlID == kIdPartMenu) {
                    paintPartMenu(*dis);
                    return TRUE;
                }
                if (dis->CtlID == kIdDivider) {
                    paintDivider(*dis);
                    return TRUE;
                }
                if (dis->CtlID == kIdStepBoxMove) {
                    paintStepBox(*dis, true);
                    return TRUE;
                }
                if (dis->CtlID == kIdStepBoxRot) {
                    paintStepBox(*dis, false);
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
                // Al perder el foco (o con Enter/Escape) las cajitas de
                // pasos validan su texto y se quedan con el sufijo
                // ("cm"/"m" o "\xb0"), ya normalizado.
                if (HIWORD(wParam) == EN_KILLFOCUS) {
                    if (id == kIdStepEditMove && self) {
                        self->commitMoveField();
                        return 0;
                    }
                    if (id == kIdStepEditRot && self) {
                        self->commitRotField();
                        return 0;
                    }
                }
                if (HIWORD(wParam) == BN_CLICKED &&
                    id >= kIdToolBase && id < kIdToolBase + kToolCount) {
                    self->setTool(id - kIdToolBase);
                    return 0;
                }
                if (id == kIdPart) {
                    if (self->onAddPart_) self->onAddPart_(self->partShape_);
                    return 0;
                }
                if (HIWORD(wParam) == BN_CLICKED && id == kIdPartMenu) {
                    self->showShapeMenu(!self->menuOpen_);
                    return 0;
                }
                if (HIWORD(wParam) == BN_CLICKED && id >= 1 && id <= kTabCount) {
                    self->selectTab(id - 1);
                    return 0;
                }
                break;
            }
            case WM_CTLCOLOREDIT: {
                // Las dos cajitas de pasos: texto claro sobre el mismo
                // fondo oscuro de la caja (no blanco del sistema).
                HDC hdc = reinterpret_cast<HDC>(wParam);
                SetTextColor(hdc, theme::text());
                SetBkColor(hdc, theme::boxBackground());
                return reinterpret_cast<long long>(theme::boxBackgroundBrush());
            }
            default:
                break;
        }
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

long long __stdcall WorkspaceTabs::popupProc(void* hwndPtr, unsigned int msg,
                                             unsigned long long wParam,
                                             long long lParam) {
    HWND hwnd = static_cast<HWND>(hwndPtr);
    WorkspaceTabs* self = reinterpret_cast<WorkspaceTabs*>(
        GetWindowLongPtrA(hwnd, GWLP_USERDATA));

    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<WorkspaceTabs*>(cs->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }

    if (self) {
        switch (msg) {
            case WM_ACTIVATE:
                // Al perder la activacion (clic fuera) se cierra.
                if (LOWORD(wParam) == WA_INACTIVE && self->menuOpen_) {
                    self->showShapeMenu(false);
                }
                return 0;
            case WM_DRAWITEM: {
                auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                const int id = static_cast<int>(dis->CtlID);
                if (id >= kIdShapeBase &&
                    id < kIdShapeBase + kShapeOptionCount) {
                    const int shape = kShapeOptions[id - kIdShapeBase].shape;
                    paintShapeOption(*dis, shape == self->partShape_);
                    return TRUE;
                }
                break;
            }
            case WM_COMMAND: {
                const int id = static_cast<int>(LOWORD(wParam));
                if (HIWORD(wParam) == BN_CLICKED && id >= kIdShapeBase &&
                    id < kIdShapeBase + kShapeOptionCount) {
                    self->setPartShape(kShapeOptions[id - kIdShapeBase].shape);
                    self->showShapeMenu(false);
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

void WorkspaceTabs::setPartShape(int shape) {
    partShape_ = shape;
    updatePartLabel();
}

void WorkspaceTabs::updatePartLabel() {
    if (!partButton_) return;
    const char* label = "Cube";
    for (const ShapeOption& o : kShapeOptions) {
        if (o.shape == partShape_) {
            label = o.label;
            break;
        }
    }
    SetWindowTextA(static_cast<HWND>(partButton_), label);
    InvalidateRect(static_cast<HWND>(partButton_), nullptr, TRUE);
}

void WorkspaceTabs::showShapeMenu(bool show) {
    if (!shapeMenu_) return;
    if (!show) {
        ShowWindow(static_cast<HWND>(shapeMenu_), SW_HIDE);
        menuOpen_ = false;
        return;
    }
    if (!partMenuButton_) return;
    RECT rc{};
    GetWindowRect(static_cast<HWND>(partMenuButton_), &rc);
    SetWindowPos(static_cast<HWND>(shapeMenu_), HWND_TOPMOST, rc.left,
                 rc.bottom + 2, kMenuWidth,
                 kShapeOptionCount * kMenuRowHeight, SWP_SHOWWINDOW);
    SetFocus(static_cast<HWND>(shapeMenu_));
    menuOpen_ = true;
}

// La cajita de mover/escalar: valida el numero (2 decimales) y deja el
// texto con su sufijo de unidad ("cm"/"m"). 0 desactiva el redondeo.
void WorkspaceTabs::commitMoveField() {
    HWND ed = static_cast<HWND>(stepEdits_[0]);
    if (!ed) return;
    char buf[64]{};
    GetWindowTextA(ed, buf, static_cast<int>(sizeof(buf)));
    const float v = parseMove(buf);
    if (v < 0.0f) {
        // Sin ningun digito: se restaura el paso anterior.
        SetWindowTextA(ed, formatMove(moveStep_).c_str());
        return;
    }
    float step = std::round(v * 100.0f) / 100.0f;
    if (step < 0.0f) step = 0.0f;
    if (step > 100000.0f) step = 100000.0f;
    moveStep_ = step;
    SetWindowTextA(ed, formatMove(step).c_str());
}

// La cajita de rotacion: valida el entero (1..360) y le pega el "\xb0".
void WorkspaceTabs::commitRotField() {
    HWND ed = static_cast<HWND>(stepEdits_[1]);
    if (!ed) return;
    char buf[64]{};
    GetWindowTextA(ed, buf, static_cast<int>(sizeof(buf)));
    const int deg = parseRot(buf);
    if (deg < 1) {
        SetWindowTextA(ed, formatRot(rotateStep_).c_str());
        return;
    }
    const int clamped = (deg > 360) ? 360 : deg;
    rotateStep_ = clamped;
    SetWindowTextA(ed, formatRot(clamped).c_str());
}

} // namespace sk
