#pragma once

#include <string>
#include <vector>

#include "../math/math.h"
#include "picking.h"

namespace sk {

class Scene;

// Linea de gizmo en espacio mundo con su eje (0=X, 1=Y, 2=Z; -1 =
// adorno sin interaccion, p.ej. el circulo central).
struct GizmoLine {
    Vec3 a{};
    Vec3 b{};
    Vec3 color{};
    int axis = -1;
};

// Geometria del gizmo de `tool` (1 mover, 2 escalar, 3 rotar) centrado
// en `origin`. scaleMax: mayor componente de escala en valor absoluto
// (L = 0.5*|scaleMax| + 1.0). hoveredAxis resalta ese eje mezclandolo
// 50% con blanco.
std::vector<GizmoLine> buildGizmo(int tool, const Vec3& origin, float scaleMax,
                                  const Vec3& viewDir, int hoveredAxis);

// Eje cuyo segmento queda a menos de 10 px del puntero (-1 si ninguno).
int hitTestGizmo(const std::vector<GizmoLine>& lines, const Vec2& pointer,
                 const Mat4& viewProj, int width, int height);

// Punto de referencia del eje ("punta"), siempre sobre la geometria
// interactiva: extremo del eje (mover/escalar) o punto del circulo
// (rotar). Para los registros/pruebas del viewport.
Vec3 gizmoHandlePoint(int tool, int axis, const Vec3& origin, float scaleMax);

// --- Arrastre: afecta a TODOS los objetos seleccionados (el gizmo se
// dibuja en el primario, el delta se aplica a la seleccion completa). ---

struct GizmoSnapshot {
    std::string name;
    Vec3 position{};
    Vec3 rotation{};
    Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct GizmoDrag {
    bool active = false;
    int tool = 0;
    int axis = -1;
    Vec3 origin{};   // centro del gizmo al empezar (fijo durante el drag)
    Vec3 axisDir{};  // eje de mundo unitario
    Vec3 planeN{};   // normal del plano de arrastre
    float L = 1.0f;  // tamano del gizmo al empezar
    float s0 = 0.0f; // parametro inicial (mover/escalar) o angulo (rotar)
    std::vector<GizmoSnapshot> snapshots;
};

// Empieza el arrastre con el rayo bajo el puntero. Devuelve false si el
// plano es paralelo al rayo o la seleccion esta vacia (no se toca drag).
bool gizmoBegin(GizmoDrag& drag, int tool, int axis, const Scene& scene,
                const std::vector<std::string>& selection, const Vec3& origin,
                float scaleMax, const Vec3& viewDir, const Ray& ray);

// Aplica el puntero actual a todos los objetos desde sus snapshots.
// Devuelve false si el rayo no corta el plano (se ignora ese frame).
bool gizmoUpdate(GizmoDrag& drag, const Ray& ray, Scene& scene);

// Revierte todos los objetos a sus snapshots (ESC).
void gizmoRevert(GizmoDrag& drag, Scene& scene);

} // namespace sk
