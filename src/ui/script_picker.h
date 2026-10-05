#pragma once

#include <windows.h>

#include <string>

namespace sk {

struct Project;

namespace ui {

// Dialogo modal oscuro con la lista de scripts .sk del proyecto para
// asociarlos a un objeto (ModelScript). initial es la rel que aparece
// preseleccionada ("" = "(ninguno)"). Devuelve false si se cancelo; si
// acepta, outRel queda con la rel elegida bajo scripts/ ("" = sin
// asociacion).
bool pickScript(HWND owner, const Project& project,
                const std::string& initial, std::string& outRel);

} // namespace ui
} // namespace sk
