#pragma once

#include <functional>
#include <string>

namespace sk {

struct Project;

// Division CODE: organizador de scripts. Panel izquierdo con los datos
// del nodo seleccionado (Properties) y panel derecho con el arbol de
// scripts (Server/Shared/Player/Character, carpetas anidadas y scripts
// .sk) y botones de accion. Los archivos viven en la carpeta scripts/
// del proyecto.
//
// ModelScript: el campo "Asociado a" muestra el objeto de la escena
// vinculado al script (via onQueryAssoc); borrar o renombrar un script
// avisa al dueno (onScriptRemoved/onScriptRenamed) para que actualice
// y guarde las asociaciones.
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

    // Nombre del objeto asociado a una rel de script ("" si no hay).
    void setOnQueryAssoc(std::function<std::string(const std::string&)> cb) {
        onQueryAssoc_ = std::move(cb);
    }
    // Refresca el panel Properties (p. ej. tras cambiar la asociacion
    // desde PLACE).
    void refreshProperties() { updateProperties(); }
    // Se borro el elemento rel (archivo o carpeta con sus hijos).
    void setOnScriptRemoved(std::function<void(const std::string&)> cb) {
        onScriptRemoved_ = std::move(cb);
    }
    // rel paso de oldRel a newRel (archivo o carpeta).
    void setOnScriptRenamed(
        std::function<void(const std::string&, const std::string&)> cb) {
        onScriptRenamed_ = std::move(cb);
    }

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
    std::function<std::string(const std::string&)> onQueryAssoc_;
    std::function<void(const std::string&)> onScriptRemoved_;
    std::function<void(const std::string&, const std::string&)> onScriptRenamed_;
};

} // namespace sk
