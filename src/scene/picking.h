#pragma once

#include <string>
#include <vector>

#include "../math/math.h"

namespace sk {

class Camera;
class Scene;
struct SceneObject;

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

// Igual que pickObject ignorando los nombres de `ignore` (el arrastre
// de cuerpo los descarta para exigir otro parte bajo el puntero).
std::string pickObjectExcept(const Scene& scene, const Ray& ray,
                             const std::vector<std::string>& ignore);

// Igual que pickObjectExcept pero devuelve tambien el punto de impacto
// en espacio mundo. Devuelve false si no hay hit.
bool pickObjectHit(const Scene& scene, const Ray& ray,
                   const std::vector<std::string>& ignore, std::string& nameOut,
                   Vec3& pointOut);

// Como pickObjectHit pero devuelve ademas la normal de la cara golpeada
// en espacio mundo (unitaria y hacia fuera). Es lo que usa el arrastre
// de cuerpo para apoyar el objeto en la cara bajo el puntero, sea
// cual sea su direccion (suelo, techo, pared o cara inclinada).
bool pickObjectFace(const Scene& scene, const Ray& ray,
                    const std::vector<std::string>& ignore, std::string& nameOut,
                    Vec3& pointOut, Vec3& normalOut);

// Impacto del rayo con un unico objeto: punto y normal de la cara
// golpeada en espacio mundo. false si el rayo no lo toca.
bool rayObjectFace(const SceneObject& object, const Ray& ray, Vec3& pointOut,
                   Vec3& normalOut);

// Objetos cuya caja proyectada intersecta el rectangulo (x0,y0)-(x1,y1)
// en coordenadas de cliente. x0<x1, y0<y1.
std::vector<std::string> selectInRect(const Scene& scene, const Mat4& viewProj,
                                      const Vec2& a, const Vec2& b,
                                      int width, int height);

} // namespace sk
