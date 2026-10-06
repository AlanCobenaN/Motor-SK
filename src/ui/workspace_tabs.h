#pragma once

#include <functional>

namespace sk {

// Barra superior de la vista 3D, en dos filas dentro de la misma ventana
// hija:
//  - fila de atajos (arriba): herramientas del viewport (Seleccionar /
//    Mover / Escalar / Rotar, atajos 1-4) y el boton "Part", que anade
//    un objeto 3D a la escena.
//  - navbar (abajo): las tres divisiones del workspace (PLACE / CODE /
//    GUI) como pestanas compactas.
//
// Los WM_DRAWITEM/WM_COMMAND de los botones owner-draw llegan aqui (la
// ventana hija tiene wndProc propia), no a la ventana principal.
class WorkspaceTabs {
public:
    static constexpr int kShortcutHeight = 36;
    static constexpr int kNavbarHeight = 36;
    // Alto total de la banda: los paneles PLACE/CODE empiezan aqui.
    static constexpr int kTopBandHeight = kShortcutHeight + kNavbarHeight;

    bool create(void* parentHwnd);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // 0 = PLACE, 1 = CODE, 2 = GUI.
    int active() const { return active_; }

    // Se llama cada vez que cambia la division activa.
    void setOnTabChanged(std::function<void(int)> cb) { onTabChanged_ = std::move(cb); }

    // Atajo "Part" de la barra superior: anade un objeto 3D a la escena.
    void setOnAddPart(std::function<void()> cb) { onAddPart_ = std::move(cb); }

    // Herramienta activa del viewport: 0 = Seleccionar, 1 = Mover,
    // 2 = Escalar, 3 = Rotar (teclas 1-4, botones de la fila de atajos).
    int tool() const { return tool_; }
    void setTool(int tool);

    // Se llama cada vez que cambia la herramienta activa.
    void setOnToolChanged(std::function<void(int)> cb) { onToolChanged_ = std::move(cb); }

    // width/height son los de la ventana principal; la barra solo usa
    // kTopBandHeight de alto.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    void selectTab(int index);

    static constexpr int kTabCount = 3;
    static constexpr int kIdPart = 100;
    static constexpr int kToolCount = 4;
    static constexpr int kIdToolBase = 110;

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    void* buttons_[kTabCount] = {};
    void* partButton_ = nullptr;
    void* toolButtons_[kToolCount] = {};
    int active_ = 0;
    int tool_ = 0;
    bool visible_ = false;
    std::function<void(int)> onTabChanged_;
    std::function<void()> onAddPart_;
    std::function<void(int)> onToolChanged_;
};

} // namespace sk
