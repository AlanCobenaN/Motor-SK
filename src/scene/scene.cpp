#include "scene.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace sk {

Mat4 SceneObject::modelMatrix() const {
    // sk::scale explicito: el miembro scale oculta a la funcion free.
    return translate(position) * rotateX(rotation.x) * rotateY(rotation.y) *
           rotateZ(rotation.z) * sk::scale(scale);
}

SceneObject& Scene::addPart() {
    SceneObject obj;
    // El nombre se deduce de cuantos objetos hay; si ese nombre ya
    // existe (escena cargada de archivo), se busca el siguiente libre.
    size_t index = objects_.size();
    obj.name = index == 0 ? "Part" : "Part" + std::to_string(index);
    while (findByName(obj.name)) {
        ++index;
        obj.name = "Part" + std::to_string(index);
    }
    obj.position = {static_cast<float>(index % 4) * 1.6f - 2.4f, 0.5f,
                    static_cast<float>(index / 4) * 1.6f};
    objects_.push_back(std::move(obj));
    return objects_.back();
}

void Scene::clear() {
    objects_.clear();
}

const SceneObject* Scene::findByName(const std::string& name) const {
    for (const SceneObject& object : objects_) {
        if (object.name == name) return &object;
    }
    return nullptr;
}

SceneObject* Scene::findByName(const std::string& name) {
    return const_cast<SceneObject*>(
        static_cast<const Scene*>(this)->findByName(name));
}

bool Scene::renameObject(const std::string& oldName,
                         const std::string& newName) {
    if (newName.empty() || oldName == newName) return false;
    if (findByName(newName)) return false;
    SceneObject* object = findByName(oldName);
    if (!object) return false;
    object->name = newName;
    return true;
}

namespace {

// "  a b " -> "a b" (recorta espacios por ambos lados).
std::string trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

void writeVec3(std::ofstream& file, const char* key, const Vec3& v) {
    char buf[96]{};
    snprintf(buf, sizeof(buf), "%s: %.6g %.6g %.6g\n", key, v.x, v.y, v.z);
    file << buf;
}

// "1.5 0 -2" -> Vec3. Devuelve false si no hay al menos 3 numeros
// (en ese caso out no se toca).
bool parseVec3(const std::string& text, Vec3& out) {
    const char* p = text.c_str();
    char* end = nullptr;
    float v[3]{};
    for (int i = 0; i < 3; ++i) {
        v[i] = strtof(p, &end);
        if (end == p) return false;
        p = end;
    }
    out = {v[0], v[1], v[2]};
    return true;
}

} // namespace

bool Scene::loadFromFile(const std::string& path) {
    objects_.clear();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return true; // escena nueva

    std::ifstream file(path);
    if (!file.is_open()) return false;

    SceneObject current;
    bool haveObject = false;
    std::string line;
    while (std::getline(file, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = trim(line.substr(0, colon));
        const std::string value = trim(line.substr(colon + 1));

        if (key == "objeto") {
            if (haveObject) objects_.push_back(current);
            current = SceneObject{};
            current.name = value;
            haveObject = true;
        } else if (!haveObject) {
            continue; // "version" y claves sueltas antes del primer objeto
        } else if (key == "posicion") {
            parseVec3(value, current.position);
        } else if (key == "rotacion") {
            parseVec3(value, current.rotation);
        } else if (key == "escala") {
            parseVec3(value, current.scale);
        } else if (key == "script") {
            current.script = value;
        }
    }
    if (haveObject) objects_.push_back(current);
    return true;
}

bool Scene::saveToFile(const std::string& path) const {
    const std::filesystem::path fsPath(path);
    if (fsPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(fsPath.parent_path(), ec);
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;

    file << "version: 1\n";
    for (const SceneObject& object : objects_) {
        file << "objeto: " << object.name << "\n";
        writeVec3(file, "posicion", object.position);
        writeVec3(file, "rotacion", object.rotation);
        writeVec3(file, "escala", object.scale);
        if (!object.script.empty()) file << "script: " << object.script << "\n";
    }
    return file.good();
}

} // namespace sk
