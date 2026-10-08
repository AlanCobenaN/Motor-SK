#pragma once

#include <vector>

#include "../scene/scene.h"

namespace sk {

// Vertice de malla: posicion + color por vertice (el shader de
// triangulos lee exactamente el mismo layout que las lineas de la
// rejilla). El sombreado es plano: el gris de la cara se calcula al
// generar la malla a partir de su normal, asi que no hay normales en
// el vertice ni luz en el shader.
struct MeshVertex {
    float pos[3];
    float color[3];
};

// Numero de formas con malla propia (comprobado contra
// Shape::Count con un static_assert en renderer.cpp).
inline constexpr int kShapeMeshes = 7;

// Genera la malla low-poly de la forma, centrada en el origen y
// contenida en la caja unitaria [-0.5, 0.5]^3: el SceneObject la
// escala con su matriz modelo, igual que el cubo de siempre. Los
// triangulos van sin indice (cada tres vertices) y con el cull
// desactivado en el renderer, asi que el sentido no importa.
void buildShapeMesh(Shape shape, std::vector<MeshVertex>& out);

} // namespace sk
