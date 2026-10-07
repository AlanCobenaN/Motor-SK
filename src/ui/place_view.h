#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../scene/scene.h"

namespace sk {

// Paneles laterales de la division PLACE: Properties (Nombre editable,
// fila Parent de solo lectura y Transform) a la izquierda y el Explorer
// (arbol con la raiz dimension01) a la derecha, sobre la vista 3D. Son
// dos ventanas hijas de la principal (con WS_CLIPCHILDREN para que el
// present no las tape).
//
// El Explorer arranca con una unica raiz "dimension01" (la escena del
// lugar) y recibe los objetos anadidos con addObject(). Properties
// muestra el Nombre del objeto seleccionado en un campo editable (solo
// habilitado con un parte seleccionado), su Parent (la raiz
// dimension01; solo lectura, pintado apagado) y el Transform en
// cascada: "Transform" se pliega y dentro Position/Rotation/Scale
// muestran un resumen "x, y, z" editable y los tres ejes al
// desplegarlas.
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

    // Seleccion multiple proveniente del viewport: mueve el caret del
    // arbol al objeto "primary" sin disparar onSelectionChanged (el
    // dueno ya tiene el estado y pinta el con showObject/showMultiple).
    void syncTreeSelection(const std::string& primary);

    // Properties con varios objetos seleccionados: estado "-N objetos
    // seleccionados-" y el resto de campos apagados.
    void showMultiple(int count);

    // Se invoca con el nombre del nodo seleccionado ("" si no hay
    // seleccion). El dueno decide que hacer con la escena.
    void setOnSelectionChanged(std::function<void(const std::string&)> cb) {
        onSelectionChanged_ = std::move(cb);
    }

    // Campo Transform (ids 200..208) editado: el usuario tecleo un valor
    // y el campo perdio el foco. El dueno lo aplica al objeto y guarda.
    void setOnTransformEdited(
        std::function<void(const std::string&, int id, float value)> cb) {
        onTransformEdited_ = std::move(cb);
    }

    // Campo Nombre (id 100) editado: el usuario tecleo un nombre y
    // perdio el foco o pulso Enter. El dueno valida, renombra el objeto
    // y guarda; si devuelve false el panel restaura el texto anterior.
    void setOnNameEdited(
        std::function<bool(const std::string& oldName,
                           const std::string& newName)> cb) {
        onNameEdited_ = std::move(cb);
    }

    // Renombra el nodo del Explorer (oldName -> newName) sin disparar
    // onSelectionChanged: el caret sigue en el mismo nodo.
    void renameTreeItem(const std::string& oldName, const std::string& newName);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    void layoutPanels(int width, int height);
    void layoutProperties();
    void updateSummaries();
    void notifySelection();

    void* parent_ = nullptr;
    void* explorer_ = nullptr;
    void* properties_ = nullptr;
    void* tree_ = nullptr;
    void* root_ = nullptr;   // HTREEITEM de dimension01
    bool visible_ = false;
    bool suppressNotify_ = false;  // syncTreeSelection mueve el caret a mano
    std::string selectedName_;   // objeto seleccionado ("" si no hay)
    // Ultimo texto mostrado en cada campo Transform (para restaurarlo si
    // la edicion no es un numero o no hay objeto seleccionado).
    std::string fieldText_[9];
    // Estado de las cascadas del panel Transform: todo desplegado al
    // arrancar. tri*_ es la zona de click del triangulito (cliente del
    // panel: x0, y0, x1, y1; vacia si la cascada no se muestra).
    bool transformOpen_ = true;
    bool rowOpen_[3] = {false, false, false};
    int triTransform_[4] = {};
    int triRow_[3][4] = {};
    // Ultimo texto de los resumenes "x, y, z" (ids 300..302).
    std::string summaryText_[3];
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&, int, float)> onTransformEdited_;
    std::function<bool(const std::string&, const std::string&)> onNameEdited_;
};

} // namespace sk
