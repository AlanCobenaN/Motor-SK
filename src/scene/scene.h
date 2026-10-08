#pragma once

#include <string>
#include <vector>

#include "../math/math.h"

namespace sk {

// Forma geometrica de un Part. Mismos nombres que en Roblox (Block,
// Ball, Cylinder, Wedge, CornerWedge) con el rombo (octaedro) como
// extra low-poly. El cubo es el valor por defecto y el indice 0, asi
// que las escenas antiguas sin "forma" cargan tal cual.
enum class Shape : int {
    Cube = 0,      // cubo
    Rhombus,       // rombo (octaedro: 8 caras)
    Sphere,        // esfera low-poly (8x4 = 48 triangulos)
    Cylinder,      // cilindro con el eje en X (como en Roblox)
    Wedge,         // cuna: prisma triangular con la pendiente en +Z
    CornerWedge,   // cuna de esquina: tetraedro de 3 tri. rectos
    Capsule,       // capsula: cilindro con casquetes semiesfericos
    Count,
};

// Nombre canonico de la forma para el archivo de escena ("forma: ...").
// Devuelve "cubo" para formas desconocidas (y shapeFromName acepta
// cualquier texto sin romper la carga).
const char* shapeName(Shape shape);
Shape shapeFromName(const std::string& name);

// Objeto minimo de la escena PLACE: primitiva "Part" con su
// transformacion (traslacion, rotacion en grados y escala), su forma
// geometrica y su asociacion ModelScript (ruta relativa de scripts/ o
// "" si no hay).
struct SceneObject {
    std::string name;   // nombre visible en el Explorer
    Vec3 position{};
    Vec3 rotation{};    // grados en los ejes X/Y/Z
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Shape shape = Shape::Cube;
    std::string script; // rel bajo scripts/ ("Server/Saludo.sk")

    // Appearance
    bool castShadow = true;
    float reflectance = 0.0f;   // 0.00 a 1.00
    float transparency = 0.0f;  // 0.00 a 1.00

    // Data
    bool locked = false;

    // Pivot (origen de la part, independiente de position)
    Vec3 pivotPosition{};
    Vec3 pivotRotation{};  // grados en los ejes X/Y/Z

    // Collision
    bool canCollide = true;
    bool anchored = false;

    // T * Rx * Ry * Rz * S (las rotaciones se aplican en este orden).
    Mat4 modelMatrix() const;
};

// Coleccion de objetos de la division PLACE. Solo la muta la interfaz
// (boton "Part"); el renderer la lee por const referencia cada frame.
class Scene {
public:
    // Anade un Part en una fila de la rejilla (Part, Part1, Part2...)
    // con la forma indicada (cubo por defecto). Devuelve una referencia
    // al ultimo elemento; no conservarla tras cualquier otra llamada a
    // addPart/clear.
    SceneObject& addPart(Shape shape = Shape::Cube);

    void clear();

    const SceneObject* findByName(const std::string& name) const;
    SceneObject* findByName(const std::string& name);

    // Renombra un objeto (transform y script intactos). Requiere que el
    // origen exista y que el destino este vacio: devuelve false sin
    // tocar nada en caso contrario.
    bool renameObject(const std::string& oldName, const std::string& newName);

    // Elimina un objeto por nombre. Devuelve false si no existe.
    bool removeObject(const std::string& name);

    // Duplica un objeto: copia todos sus campos con un nombre nuevo
    // (Part, Part1...) y su posicion desplazada por "offset". Devuelve
    // una referencia al nuevo objeto.
    SceneObject& addCopy(const SceneObject& src, const Vec3& offset);

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
