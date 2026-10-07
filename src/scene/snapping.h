#pragma once

#include <string>
#include <vector>

#include "../math/math.h"
#include "picking.h"

namespace sk {

class Scene;

// --- Arrastre de partes estilo Roblox Studio (surface snapping) ---
//
// Mientras se arrastra el cuerpo de un parte:
//
//  - Si el puntero esta sobre otro parte, la caja del grupo arrastrado
//    se apoya en ESA cara (punto de golpe + soporte de la caja a lo
//    largo de la normal): sirve para suelo, techo, pared o cualquier
//    cara inclinada. La rotacion del lead se alinea a la cara del
//    destino (Alt la conserva) y su posicion en el plano de la cara se
//    redondea a `grid` relativa al centro del destino (Shift, grid 0,
//    la deja libre). El flush a lo largo de la normal es exacto.
//
//  - Sin nada bajo el puntero, el rayo corta el plano del suelo Y=0 y
//    el grupo queda con la caja inferior apoyada en el suelo (si el
//    rayo no llega al suelo, el punto de agarre sigue al puntero en el
//    plano de camara).
//
// El resto del grupo se mantiene rigido alrededor del lead (Delta R
// aplicado a posiciones y rotaciones iniciales).

// Paso de rejilla por defecto, en unidades de mundo (tipo "studs" de
// Roblox). 0 desactiva el redondeo (Shift durante el arrastre).
constexpr float kDragGrid = 1.0f;

// Pose (posicion + rotacion en grados) de un objeto arrastrado.
struct DragPose {
    std::string name;
    Vec3 position{};
    Vec3 rotation{};
};

// Salida del calculo de un frame de arrastre.
struct DragResult {
    bool onSurface = false;   // hay cara de otro parte bajo el puntero
    std::string surfaceName;  // parte destino ("" si no lo hay)
    Vec3 surfaceNormal{};     // normal de esa cara (mundo, unitaria)
    bool groundHit = false;   // sin superficie: el rayo alcanzo Y=0 (si
                              // no, miro al cielo y manda el plano de
                              // camara del punto de agarre)
    std::vector<DragPose> poses; // nuevas poses, mismo orden que `dragged`
};

// Calcula las poses del grupo arrastrado para este frame.
//   ray0: rayo del punto de presion (para el fallback de plano de camara)
//   ray1: rayo actual del puntero
//   dragged / startPose: nombres y poses iniciales del grupo (mismo orden)
//   leadIndex: indice del objeto agarrado dentro de `dragged`
//   grabOffset: punto de agarre - centro del lead al empezar (mundo)
//   alignSurface: false con Alt pulsado = conservar la orientacion inicial
//   grid: paso de rejilla; <= 0 sin redondeo
void computeSurfaceDrag(const Scene& scene, const Ray& ray0, const Ray& ray1,
                        const std::vector<std::string>& dragged, int leadIndex,
                        const Vec3& grabOffset,
                        const std::vector<DragPose>& startPose,
                        bool alignSurface, float grid, DragResult& out);

} // namespace sk
