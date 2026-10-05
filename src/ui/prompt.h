#pragma once

#include <windows.h>

#include <string>

namespace sk {
namespace ui {

// Dialogo modal de texto (Win32 no tiene InputBox). Devuelve el texto
// aceptado o "" si se cancelo.
std::string promptText(HWND owner, const char* title, const char* initial);

} // namespace ui
} // namespace sk
