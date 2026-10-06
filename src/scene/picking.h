#pragma once

#include <string>
#include <vector>

#include "../math/math.h"

namespace sk {

class Camera;
class Scene;

// Seleccion en el viewport 3D de PLACE:
//  - pickObject: rayo desde la camara por un pixel -> objeto mas cercano.
//  - selectInRect: proyecta las cajas de los objetos y devuelve las que
//    tocan el rectangulo de arrastre (marquee), en coordenadas de cliente.

// Rayo en espacio mundo desde la posicion de un pixel de cliente.
struct Ray {
    Vec3 origin{};
    Vec3 dir{};
};

Ray rayFromCamera(const Camera& camera, float aspect, float fovYDegrees,
                  const Vec2& pixel, int width, int height);

// Nombre del objeto mas cercano tocado por el rayo ("" si no hay hit).
std::string pickObject(const Scene& scene, const Ray& ray);

// Objetos cuya caja proyectada intersecta el rectangulo (x0,y0)-(x1,y1)
// en coordenadas de cliente. x0<x1, y0<y1.
std::vector<std::string> selectInRect(const Scene& scene, const Mat4& viewProj,
                                      const Vec2& a, const Vec2& b,
                                      int width, int height);

} // namespace sk
