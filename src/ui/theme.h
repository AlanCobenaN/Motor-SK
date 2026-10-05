#pragma once

#include <windows.h>
#include <dwmapi.h>

namespace sk {

// Tema oscuro de la interfaz Win32: colores y pintado de botones
// redondeados (owner-draw). Los botones se crean con BS_OWNERDRAW y el
// padre los pinta en WM_DRAWITEM con paintDarkButton.
namespace theme {

inline COLORREF background()    { return RGB(24, 24, 28); }
inline COLORREF listBackground() { return RGB(18, 18, 22); }
inline COLORREF surface()       { return RGB(54, 54, 60); }
inline COLORREF surfaceHover()  { return RGB(74, 74, 82); }
inline COLORREF surfacePressed(){ return RGB(40, 40, 46); }
inline COLORREF border()        { return RGB(88, 88, 96); }
inline COLORREF text()          { return RGB(238, 238, 242); }
inline COLORREF textDisabled()  { return RGB(120, 120, 128); }
inline COLORREF accent()        { return RGB(54, 106, 196); }
inline COLORREF headerBackground() { return RGB(34, 34, 40); }

// Pinceles de fondo para clases de ventana y controles.
inline HBRUSH backgroundBrush() {
    static HBRUSH brush = CreateSolidBrush(background());
    return brush;
}

inline HBRUSH surfaceBrush() {
    static HBRUSH brush = CreateSolidBrush(surface());
    return brush;
}

inline HBRUSH listBackgroundBrush() {
    static HBRUSH brush = CreateSolidBrush(listBackground());
    return brush;
}

// Tipografia de la interfaz: Segoe UI (mas grande que el DEFAULT_GUI_FONT).
inline HFONT uiFont() {
    static HFONT font = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    return font;
}

// Fuente de las cabeceras del ListView: un punto menor y semibold.
inline HFONT uiHeaderFont() {
    static HFONT font = CreateFontW(
        -15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    return font;
}

// Barra de titulo oscura (Win10 1809+; en versiones anteriores el
// atributo se ignora y devolvemos sin mas).
inline void enableDarkTitleBar(HWND hwnd) {
    BOOL enable = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &enable, sizeof(enable)))) {
        DwmSetWindowAttribute(hwnd, 19, &enable, sizeof(enable));
    }
}

// Pinta un boton redondo con el texto centrado.
// hover: estado de pasada del raton (subclase en el boton).
inline void paintDarkButton(const DRAWITEMSTRUCT& dis, bool hover) {
    HDC hdc = dis.hDC;
    RECT rc = dis.rcItem;

    COLORREF fill = surface();
    if (dis.itemState & (ODS_DISABLED | ODS_SELECTED)) {
        fill = surfacePressed();
    } else if (hover) {
        fill = surfaceHover();
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border());
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 14, 14);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(brush);

    char label[128]{};
    GetWindowTextA(dis.hwndItem, label, static_cast<int>(sizeof(label)));

    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, uiFont()));
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, (dis.itemState & ODS_DISABLED) ? textDisabled() : text());
    DrawTextA(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

} // namespace theme
} // namespace sk
