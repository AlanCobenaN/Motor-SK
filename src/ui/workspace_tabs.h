#pragma once

#include <functional>

namespace sk {

// Barra superior de la vista 3D, en dos filas dentro de la misma ventana
// hija:
//  - fila de atajos (arriba): herramientas del viewport (Select /
//    Move / Scale / Rotate, atajos 1-4) y el boton "Part", que anade
//    un objeto 3D a la escena.
//  - navbar (abajo): las tres divisiones del workspace (PLACE / CODE /
//    GUI) como pestanas compactas.
//
// Los WM_DRAWITEM/WM_COMMAND de los botones owner-draw llegan aqui (la
// ventana hija tiene wndProc propia), no a la ventana principal.
class WorkspaceTabs {
public:
    // Fila de atajos (arriba): tarjetas compactas de 56x52 con el icono
    // arriba y el nombre abajo; la navbar ocupa el resto (banda total
    // de 86px).
    static constexpr int kShortcutHeight = 54;
    static constexpr int kNavbarHeight = 32;
    // Alto total de la banda: los paneles PLACE/CODE empiezan aqui.
    static constexpr int kTopBandHeight = kShortcutHeight + kNavbarHeight;

    bool create(void* parentHwnd);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // 0 = PLACE, 1 = CODE, 2 = GUI.
    int active() const { return active_; }

    // Se llama cada vez que cambia la division activa.
    void setOnTabChanged(std::function<void(int)> cb) { onTabChanged_ = std::move(cb); }

    // Atajo "Part": anade un objeto 3D a la escena con una forma
    // determinada (0 = cubo). El boton Part llama a cb(0), el menu de
    // formas llama con el indice de la forma elegida.
    void setOnAddPart(std::function<void(int)> cb) { onAddPart_ = std::move(cb); }

    // Herramienta activa del viewport: 0 = Select, 1 = Move,
    // 2 = Scale, 3 = Rotate (teclas 1-4, botones de la fila de atajos).
    int tool() const { return tool_; }
    void setTool(int tool);

    // Se llama cada vez que cambia la herramienta activa.
    void setOnToolChanged(std::function<void(int)> cb) { onToolChanged_ = std::move(cb); }

    // Forma actual del boton Part (indice de sk::Shape). Cambia desde el
    // triangulito desplegable; el boton principal crea esa forma.
    int partShape() const { return partShape_; }

    // Pasos de la barra: la cajita de arriba (mover/escalar/arrastrar)
    // da el paso de rejilla en unidades de mundo (0 = sin redondeo) y la
    // de abajo el paso de rotacion en grados enteros (1..360). El dueno
    // del arrastre los lee cada frame.
    float moveStep() const { return moveStep_; }
    int rotateStep() const { return rotateStep_; }

    // width/height son los de la ventana principal; la barra solo usa
    // kTopBandHeight de alto.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);
    static long long __stdcall popupProc(void* hwnd, unsigned int msg,
                                         unsigned long long wParam, long long lParam);
    void selectTab(int index);
    void setPartShape(int shape);
    void updatePartLabel();
    void showShapeMenu(bool show);
    void commitMoveField();
    void commitRotField();

    static constexpr int kTabCount = 3;
    static constexpr int kIdPart = 100;
    static constexpr int kIdPartMenu = 120;   // triangulito del boton Part
    static constexpr int kIdShapeBase = 200;  // opciones del desplegable
    static constexpr int kShapeCount = 7;
    static constexpr int kToolCount = 4;
    static constexpr int kIdToolBase = 110;
    static constexpr int kIdDivider = 125;    // rallita entre Rotate y Cube
    static constexpr int kIdStepBoxMove = 130; // cajita de pasos (icono)
    static constexpr int kIdStepBoxRot = 131;
    static constexpr int kIdStepEditMove = 140; // cajitas de pasos (texto)
    static constexpr int kIdStepEditRot = 141;

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    void* buttons_[kTabCount] = {};
    void* partButton_ = nullptr;
    void* partMenuButton_ = nullptr;  // triangulito dentro del boton Part
    void* shapeMenu_ = nullptr;       // ventana emergente de formas
    void* shapeButtons_[kShapeCount] = {};
    void* toolButtons_[kToolCount] = {};
    void* divider_ = nullptr;          // rallita entre Rotate y Cube
    void* stepBoxes_[2] = {};          // cajitas de pasos (icono pintado)
    void* stepEdits_[2] = {};          // cajitas de pasos (texto)
    int active_ = 0;
    int tool_ = 0;
    int partShape_ = 0;
    float moveStep_ = 1.0f;   // paso mover/escalar/arrastrar (mundo)
    int rotateStep_ = 15;     // paso de rotacion en grados
    bool menuOpen_ = false;
    bool visible_ = false;
    std::function<void(int)> onTabChanged_;
    std::function<void(int)> onAddPart_;
    std::function<void(int)> onToolChanged_;
};

} // namespace sk
