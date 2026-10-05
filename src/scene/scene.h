#pragma once

#include <string>
#include <vector>

#include "../math/math.h"

namespace sk {

// Objeto minimo de la escena PLACE: primitiva "Part" con su
// transformacion (traslacion, rotacion en grados y escala) y su
// asociacion ModelScript (ruta relativa de scripts/ o "" si no hay).
struct SceneObject {
    std::string name;   // nombre visible en el Explorer
    Vec3 position{};
    Vec3 rotation{};    // grados en los ejes X/Y/Z
    Vec3 scale{1.0f, 1.0f, 1.0f};
    std::string script; // rel bajo scripts/ ("Server/Saludo.sk")

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
    SceneObject* findByName(const std::string& name);
    const std::vector<SceneObject>& objects() const { return objects_; }
    std::vector<SceneObject>& objects() { return objects_; }

    // Persistencia en escenas/inicio.scene (texto "clave: valor",
    // versionado). Un archivo ausente o vacio = escena vacia.
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

private:
    std::vector<SceneObject> objects_;
};

} // namespace sk
