#pragma once

#include <windows.h>

namespace sk {
namespace ui {

// Botones oscuros owner-draw con estado hover: makeDarkButton subclasa el
// boton (WM_MOUSEMOVE/WM_MOUSELEAVE) y paintDarkButton lo pinta en el
// WM_DRAWITEM del padre. La lista es compartida por eso el struct es
// publico (para limpiar el hover al deshabilitar un boton).
struct DarkButton {
    HWND hwnd;
    bool hover;
};

void makeDarkButton(HWND hwnd);
DarkButton* findDarkButton(HWND hwnd);

// Pinta el boton si es nuestro (ODT_BUTTON de la lista). Devuelve true si
// lo pinto; el caller debe devolver TRUE en ese caso.
bool paintDarkButton(const DRAWITEMSTRUCT& dis);

} // namespace ui
} // namespace sk
