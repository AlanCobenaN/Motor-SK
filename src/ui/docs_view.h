#pragma once

#include <string>
#include <vector>

namespace sk {

// Una clase del motor: nombre visible, "logo" (inicial/emoji ASCII) y las
// descripciones corta (lista) y larga (detalle).
struct ClassInfo {
    const char* className;
    const char* category;
    const char* shortDesc;
    const char* longDesc;
};

// Vista "Documentacion de clases": reemplaza al viewport 3D. A la izquierda
// la lista completa de clases (logo + nombre + descripcion corta); a la
// derecha, separado por una linea, la descripcion detallada y la tabla
// "Propiedades" con los valores por defecto (vacia de momento).
class DocsView {
public:
    static constexpr int kListWidth = 320;

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

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    void* list_ = nullptr;        // lista de clases (izquierda)
    void* divider_ = nullptr;     // linea divisoria vertical
    void* title_ = nullptr;       // nombre de la clase seleccionada
    void* desc_ = nullptr;        // descripcion detallada (multilinea)
    void* propsLabel_ = nullptr;  // rotulo "Propiedades"
    void* props_ = nullptr;       // tabla de propiedades (vacia por ahora)
    int sel_ = -1;
    bool visible_ = false;
    std::vector<ClassInfo> classes_;
};

} // namespace sk
