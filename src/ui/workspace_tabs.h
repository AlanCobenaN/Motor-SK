#pragma once

namespace sk {

// Banda superior de la vista 3D: las tres divisiones del workspace
// (Objetos / Scripts / GUI) como botones redondeados sobre fondo oscuro,
// y un area vacia debajo de la division activa.
//
// Es una ventana hija con wndProc propia (los WM_DRAWITEM/WM_COMMAND de
// los botones owner-draw no llegan a la ventana principal). Altura fija.
class WorkspaceTabs {
public:
    // Alto fijo de la banda superior.
    static constexpr int kBandHeight = 260;

    bool create(void* parentHwnd);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal; la banda solo usa
    // kBandHeight de alto.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    void selectTab(int index);

    static constexpr int kTabCount = 3;

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    void* buttons_[kTabCount] = {};
    void* area_ = nullptr;
    int active_ = 0;
    bool visible_ = false;
};

} // namespace sk
