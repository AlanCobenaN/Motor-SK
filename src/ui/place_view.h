#pragma once

namespace sk {

// Paneles laterales de la division PLACE: el Explorer (arbol de Folders y
// objetos) a la izquierda y Properties (Transform) a la derecha, sobre la
// vista 3D. Son dos ventanas hijas de la principal (con WS_CLIPCHILDREN
// para que el present no las tape).
//
// Por ahora la estructura es la del spec: Explorer con dos raices
// (Folders / Objects) y Properties con el bloque Transform de solo lectura
// hasta que haya objetos en la escena.
class PlaceView {
public:
    static constexpr int kExplorerWidth = 260;
    static constexpr int kPropertiesWidth = 300;

    bool create(void* parentHwnd);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal; los paneles ocupan
    // desde kToolbarHeight hasta el borde inferior.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    void layoutPanels(int width, int height);

    void* parent_ = nullptr;
    void* explorer_ = nullptr;
    void* properties_ = nullptr;
    void* tree_ = nullptr;
    bool visible_ = false;
};

} // namespace sk
