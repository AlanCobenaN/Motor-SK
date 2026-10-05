#pragma once

#include <string>

namespace sk {

struct Project;

// Division CODE: organizador de scripts. Panel izquierdo con el arbol de
// scripts (Server/Shared/Player/Character, carpetas anidadas y scripts
// .sk) y botones de accion; panel derecho con los datos del nodo
// seleccionado. Los archivos viven en la carpeta scripts/ del proyecto.
//
// La asociacion de scripts con objetos de la escena llega con ModelScript.
class CodeView {
public:
    static constexpr int kTreeWidth = 340;
    static constexpr int kPropsWidth = 300;

    bool create(void* parentHwnd);
    void destroy();

    // Reconstruye el arbol desde scripts/ del proyecto (y lo crea si falta).
    void open(const Project& project);
    void close();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);

    void layoutPanels(int width, int height);
    void rebuildTree();
    void selectRel(const std::string& rel);
    void updateProperties();
    void updateButtons();

    std::string selectedRel() const;
    std::string parentForNew() const;
    void onNewScript();
    void onNewFolder();
    void onRename();
    void onDelete();

    void* parent_ = nullptr;
    void* scriptsPanel_ = nullptr;
    void* propsPanel_ = nullptr;
    void* tree_ = nullptr;
    void* btnNewScript_ = nullptr;
    void* btnNewFolder_ = nullptr;
    void* btnRename_ = nullptr;
    void* btnDelete_ = nullptr;
    void* propsStatus_ = nullptr;
    void* propsName_ = nullptr;
    void* propsType_ = nullptr;
    void* propsLoc_ = nullptr;
    void* propsAssoc_ = nullptr;
    const Project* project_ = nullptr;
    bool visible_ = false;
};

} // namespace sk
