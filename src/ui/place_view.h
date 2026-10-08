#pragma once

#include <functional>
#include <string>
#include <vector>

#include <windows.h>

#include "../scene/scene.h"

namespace sk {

// Fila de fondo (cajita) del panel Properties: rectangulo en coordenadas
// de contenido (sin el desplazamiento) y sombreado. shade: 0 = base de
// la cajita, 1 = fila alterna mas clara, 2 = cabecera de la cajita.
struct RowBand {
    RECT rc;
    int shade;
};

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
    // "Sin objeto seleccionado". showObject refresca valores y, salvo que
    // relayout sea false, recoloca los controles (el dueno lo pasa false
    // mientras arrastra un objeto/gizmo para no rehacer el panel cada
    // frame, que es lo que hace que parpadee).
    void showObject(const SceneObject& object, bool relayout = true);
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

    // Checkbox de Appearance/Collision/Data (ids 400..405): el usuario
    // alterna el valor. El dueno lo aplica al objeto y guarda.
    void setOnBoolEdited(
        std::function<void(const std::string&, int id, bool value)> cb) {
        onBoolEdited_ = std::move(cb);
    }

    // Campo float de Appearance (ids 410..411): Reflectance/Transparency.
    // El dueno lo aplica al objeto (clamado 0..1) y guarda.
    void setOnFloatEdited(
        std::function<void(const std::string&, int id, float value)> cb) {
        onFloatEdited_ = std::move(cb);
    }

    // Campo Pivot (ids 500..508): Position/Orientation del pivote.
    // El dueno lo aplica al objeto y guarda.
    void setOnPivotEdited(
        std::function<void(const std::string&, int id, float value)> cb) {
        onPivotEdited_ = std::move(cb);
    }

    // Renombra el nodo del Explorer (oldName -> newName) sin disparar
    // onSelectionChanged: el caret sigue en el mismo nodo.
    void renameTreeItem(const std::string& oldName, const std::string& newName);

    // Quita el nodo del Explorer (por nombre) sin disparar
    // onSelectionChanged: el dueno ya gestiona la seleccion.
    void removeTreeItem(const std::string& name);

    // Estado de un checkbox de bool (ids 400..403): cambia el valor y
    // repinta la casilla solo si cambio. Publica porque tambien la usan
    // los helpers internos del panel (resetPartFields).
    void setCheck(int id, bool on);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    void layoutPanels(int width, int height);
    void layoutProperties();
    void updateSummaries();
    void notifySelection();
    void scrollBy(int dy);

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
    // Estado de las cascadas del panel: una por categoria (Appearance,
    // Data, Transform, Pivot, Collision) y, dentro de Transform/Pivot,
    // una por fila. tri*_ es la zona de click del triangulito (cliente
    // del panel: x0, y0, x1, y1; vacia si la cascada no se muestra).
    bool catOpen_[5] = {true, true, true, true, true};
    bool rowOpen_[3] = {false, false, false};
    bool pivotRowOpen_[2] = {false, false};
    // Estado de los checkboxes (ids 400..403): BS_OWNERDRAW solo, asi que
    // el estado lo llevamos aqui (BM_GETCHECK requiere BS_CHECKBOX, que
    // no se puede combinar con BS_OWNERDRAW sin perder los clicks).
    bool checkState_[4] = {};
    int triCat_[5][4] = {};
    int triRow_[3][4] = {};
    int triPivotRow_[2][4] = {};
    // Ultimo texto de los resumenes "x, y, z" (ids 300..302).
    std::string summaryText_[3];
    // Resumenes "x, y, z" de las filas del Pivot (ids 310..311).
    std::string pivotSummaryText_[2];
    // Si es false no se muestran las categorias (seleccion vacia: solo
    // las Parts tienen propiedades).
    bool showCategories_ = true;
    // Ultimo texto valido de Reflectance/Transparency (410..411) y de
    // los campos Pivot (500..505): se restaura si la edicion no es un
    // numero o no hay objeto seleccionado.
    std::string floatText_[2];
    std::string pivotText_[6];
    // Fondos de las cajitas (bandas zebra + bordes) en coordenadas de
    // contenido y desplazamiento vertical del panel con scroll.
    std::vector<RowBand> bands_;
    std::vector<RECT> boxes_;
    int scrollY_ = 0;
    int scrollContent_ = 0;
    std::function<void(const std::string&)> onSelectionChanged_;
    std::function<void(const std::string&, int, float)> onTransformEdited_;
    std::function<bool(const std::string&, const std::string&)> onNameEdited_;
    std::function<void(const std::string&, int, bool)> onBoolEdited_;
    std::function<void(const std::string&, int, float)> onFloatEdited_;
    std::function<void(const std::string&, int, float)> onPivotEdited_;
};

} // namespace sk
