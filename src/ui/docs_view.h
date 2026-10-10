#pragma once

#include <windows.h>

namespace sk {

// Vista "Documentacion de clases": reemplaza al viewport 3D. A la izquierda
// la lista completa de clases (icono + nombre + descripcion corta); a la
// derecha, separado por una linea, la categoria, la descripcion detallada
// y la tabla "Propiedades" con los valores por defecto (vacia de momento).
//
// La lista es owner-draw (LVS_OWNERDRAWFIXED) para pintar los iconos como
// emojis, las filas con esquinas redondeadas y el texto en gris acorde al
// tema oscuro. Los emojis son provisionales; mas adelante se sustituyen
// por PNG.
class DocsView {
public:
    static constexpr int kListWidth = 360;
    static constexpr int kIconColWidth = 34;
    static constexpr int kNameColWidth = 168;

    bool create(void* parentHwnd);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);

    void buildClassList();
    void layout(int width, int height);
    void updateDetails();
    void paintRow(const DRAWITEMSTRUCT& dis) const;

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    void* list_ = nullptr;        // lista de clases (izquierda)
    void* divider_ = nullptr;     // linea divisoria vertical
    void* title_ = nullptr;       // nombre de la clase seleccionada
    void* category_ = nullptr;    // categoria de la clase
    void* desc_ = nullptr;        // descripcion detallada (multilinea)
    void* propsLabel_ = nullptr;  // rotulo "Propiedades"
    void* props_ = nullptr;       // tabla de propiedades (vacia por ahora)
    void* emojiFont_ = nullptr;   // fuente para pintar los emojis
    int sel_ = -1;
    bool visible_ = false;
};

} // namespace sk
