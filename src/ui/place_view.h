#pragma once

namespace sk {

// Paneles laterales de la division PLACE: Properties (Transform) a la
// izquierda y el Explorer (arbol con la raiz dimension01) a la derecha,
// sobre la vista 3D. Son dos ventanas hijas de la principal (con
// WS_CLIPCHILDREN para que el present no las tape).
//
// El Explorer arranca con una unica raiz "dimension01" (la escena del
// lugar); los objetos se anadiran cuando existan. Properties muestra el
// bloque Transform de solo lectura hasta que haya objetos que sincronizar
// con ModelScript.
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
