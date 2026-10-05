#pragma once

#include <string>
#include <vector>

#include "../math/math.h"

namespace sk {

// Objeto minimo de la escena PLACE: primitiva "Part" con su
// transformacion (traslacion, rotacion en grados y escala).
struct SceneObject {
    std::string name;   // nombre visible en el Explorer
    Vec3 position{};
    Vec3 rotation{};    // grados en los ejes X/Y/Z
    Vec3 scale{1.0f, 1.0f, 1.0f};

    // T * Rx * Ry * Rz * S (las rotaciones se aplican en este orden).
    Mat4 modelMatrix() const;
};

// Coleccion de objetos de la division PLACE. Solo la muta la interfaz
// (boton "Part"); el renderer la lee por const referencia cada frame.
class Scene {
public:
    // Anade un Part en una fila de la rejilla (Part, Part1, Part2...).
    // Devuelve una referencia al ultimo elemento; no conservarla tras
    // cualquier otra llamada a addPart/clear.
    SceneObject& addPart();

    void clear();

    const SceneObject* findByName(const std::string& name) const;
    const std::vector<SceneObject>& objects() const { return objects_; }

private:
    std::vector<SceneObject> objects_;
};

} // namespace sk
