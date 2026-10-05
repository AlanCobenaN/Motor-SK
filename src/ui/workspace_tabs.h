#pragma once

namespace sk {

// Barra superior de la vista 3D: las tres divisiones del workspace
// (PLACE / CODE / GUI) como botones redondeados. Sin area adicional:
// la vista 3D ocupa el resto de la ventana.
//
// Es una ventana hija con wndProc propia (los WM_DRAWITEM/WM_COMMAND de
// los botones owner-draw no llegan a la ventana principal). Altura fija.
class WorkspaceTabs {
public:
    // Alto fijo de la barra de botones.
    static constexpr int kToolbarHeight = 48;

    bool create(void* parentHwnd);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal; la barra solo usa
    // kToolbarHeight de alto.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    void selectTab(int index);

    static constexpr int kTabCount = 3;

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    void* buttons_[kTabCount] = {};
    int active_ = 0;
    bool visible_ = false;
};

} // namespace sk
