#include "scene.h"

namespace sk {

Mat4 SceneObject::modelMatrix() const {
    // sk::scale explicito: el miembro scale oculta a la funcion free.
    return translate(position) * rotateX(rotation.x) * rotateY(rotation.y) *
           rotateZ(rotation.z) * sk::scale(scale);
}

SceneObject& Scene::addPart() {
    SceneObject obj;
    const size_t index = objects_.size();
    if (index == 0) {
        obj.name = "Part";
    } else {
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

} // namespace sk
