#pragma once

#include <functional>
#include <string>

#include "../scene/scene.h"

namespace sk {

// Paneles laterales de la division PLACE: Properties (Transform y la
// asociacion ModelScript "Asociado a") a la izquierda y el Explorer
// (arbol con la raiz dimension01) a la derecha, sobre la vista 3D. Son
// dos ventanas hijas de la principal (con WS_CLIPCHILDREN para que el
// present no las tape).
//
// El Explorer arranca con una unica raiz "dimension01" (la escena del
// lugar) y recibe los objetos anadidos con addObject(). Properties
// muestra el Transform del objeto seleccionado (o la raiz) y su script
// asociado (ModelScript); el boton "Cambiar..." abre el selector de
// scripts via onAssocEdit.
class PlaceView {
public:
    static constexpr int kExplorerWidth = 260;
    static constexpr int kPropertiesWidth = 300;

    bool create(void* parentHwnd);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // width/height son los de la ventana principal; los paneles ocupan
    // desde WorkspaceTabs::kTopBandHeight hasta el borde inferior.
    void resize(int width, int height);

    // Explorer: inserta un hijo bajo dimension01 y lo selecciona (lo que
    // dispara onSelectionChanged). clearObjects quita todos los hijos
    // y vuelve a seleccionar la raiz.
    void addObject(const std::string& name);
    void clearObjects();

    // Properties: refresca el panel con el objeto, la raiz o el estado
    // "Sin objeto seleccionado".
    void showObject(const SceneObject& object);
    void showRoot();
    void showNoSelection();

    // Se invoca con el nombre del nodo seleccionado ("" si no hay
    // seleccion). El dueno decide que hacer con la escena.
    void setOnSelectionChanged(std::function<void(const std::string&)> cb) {
        onSelectionChanged_ = std::move(cb);
    }

    // Boton "Cambiar..." de la fila Asociado a (ModelScript): se invoca
    // con el nombre del objeto seleccionado para que el dueno abra el
    // selector de scripts y guarde la escena.
    void setOnAssocEdit(std::function<void(const std::string&)> cb) {
        onAssocEdit_ = std::move(cb);
    }

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    void layoutPanels(int width, int height);
    void notifySelection();

    void* parent_ = nullptr;
    void* explorer_ = nullptr;
    void* properties_ = nullptr;
    void* tree_ = nullptr;
    void* root_ = nullptr;   // HTREEITEM de dimension01
    bool visible_ = false;
    std::string selectedName_;   // objeto seleccionado ("" si no hay)
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&)> onAssocEdit_;
};

} // namespace sk
