#include "picking.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "camera.h"
#include "scene.h"

namespace sk {

namespace {

// Punto afine (w=1) sin division: para matrices de transformacion pura.
Vec3 affine(const Mat4& m, const Vec3& p) {
    return {
        m.at(0, 0) * p.x + m.at(1, 0) * p.y + m.at(2, 0) * p.z + m.at(3, 0),
        m.at(0, 1) * p.x + m.at(1, 1) * p.y + m.at(2, 1) * p.z + m.at(3, 1),
        m.at(0, 2) * p.x + m.at(1, 2) * p.y + m.at(2, 2) * p.z + m.at(3, 2),
    };
}

// Inversa de modelMatrix() = T * Rx * Ry * Rz * S:
//   inv = S^-1 * Rz^-1 * Ry^-1 * Rx^-1 * T^-1 (rotaciones negadas).
Mat4 inverseModel(const SceneObject& object) {
    constexpr float kMinScale = 1e-6f;
    const Vec3 s{
        std::fabs(object.scale.x) < kMinScale ? kMinScale : object.scale.x,
        std::fabs(object.scale.y) < kMinScale ? kMinScale : object.scale.y,
        std::fabs(object.scale.z) < kMinScale ? kMinScale : object.scale.z,
    };
    return scale(Vec3{1.0f / s.x, 1.0f / s.y, 1.0f / s.z}) *
           rotateZ(-object.rotation.z) * rotateY(-object.rotation.y) *
           rotateX(-object.rotation.x) * translate(Vec3{-object.position.x,
                                                        -object.position.y,
                                                        -object.position.z});
}

// Rayo contra caja unitaria [-0.5, 0.5]^3 (slab test). Devuelve el
// parametro t>=0 del primer impacto y la normal de la cara golpeada en
// espacio local (unitaria, hacia fuera de la caja y contra el rayo).
bool rayBox(const Vec3& origin, const Vec3& dir, float& tOut,
            Vec3& normalOut) {
    float tmin = -FLT_MAX;
    float tmax = FLT_MAX;
    int faceAxis = -1;      // eje de la cara que fija la entrada
    float faceSign = 0.0f;  // su signo (+1/-1) en ese eje
    for (int axis = 0; axis < 3; ++axis) {
        const float o = (&origin.x)[axis];
        const float d = (&dir.x)[axis];
        if (std::fabs(d) < 1e-8f) {
            if (o < -0.5f || o > 0.5f) return false;
            continue;
        }
        float t1 = (-0.5f - o) / d;
        float t2 = (0.5f - o) / d;
        if (t1 > t2) {
            const float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (t1 > tmin) {
            tmin = t1;
            faceAxis = axis;
            // Entrada por -0.5 con d>0 (normal -eje) o por +0.5 con d<0.
            faceSign = d > 0.0f ? -1.0f : 1.0f;
        }
        if (t2 < tmax) tmax = t2;
        if (tmin > tmax) return false;
    }
    if (tmax <= 0.0f) return false; // detras de la camara
    tOut = tmin > 0.0f ? tmin : 0.0f; // origen dentro de la caja
    normalOut = {0.0f, 0.0f, 0.0f};
    if (faceAxis >= 0) {
        (&normalOut.x)[faceAxis] = faceSign;
    } else {
        normalOut = normalize(dir) * -1.0f; // degenerado (rayo paralelo)
    }
    return true;
}

// Normal en mundo de una cara local de un objeto: R * (n/escala), sin
// mas. Como cada cara del cubo es paralela a un eje local, dividir por
// la escala (que luego se normaliza) no sesga la direccion; solo hace
// falta para que la rotacion pura la mantenga unitaria.
Vec3 faceNormalWorld(const SceneObject& object, const Vec3& localNormal) {
    constexpr float kMinScale = 1e-6f;
    const auto guard = [](float v) {
        const float a = std::fabs(v);
        return a < kMinScale ? kMinScale : a;
    };
    const Vec3 scaled{localNormal.x / guard(object.scale.x),
                      localNormal.y / guard(object.scale.y),
                      localNormal.z / guard(object.scale.z)};
    const Mat4 rot = rotateX(object.rotation.x) * rotateY(object.rotation.y) *
                     rotateZ(object.rotation.z);
    return normalize(transformDir(rot, scaled));
}

// NDC de Vulkan a pixel de cliente: x,y en [-1,1] con y hacia abajo.
Vec2 ndcToPixel(const Vec3& ndc, int width, int height) {
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(width),
            (ndc.y * 0.5f + 0.5f) * static_cast<float>(height)};
}

} // namespace

Ray rayFromCamera(const Camera& camera, float aspect, float fovYDegrees,
                  const Vec2& pixel, int width, int height) {
    const float tanHalf = std::tan(radians(fovYDegrees) * 0.5f);
    const float ndcX = 2.0f * pixel.x / static_cast<float>(width) - 1.0f;
    const float ndcY = 2.0f * pixel.y / static_cast<float>(height) - 1.0f;

    // Direccion en espacio de vista (camara mirando -Z): la proyeccion
    // invierte Y (at(1,1) = -f), de ahi el signo menos en vy.
    const Vec3 viewDir{ndcX * aspect * tanHalf, -ndcY * tanHalf, -1.0f};

    const Vec3 fwd = camera.forward();
    const Vec3 right = normalize(cross(fwd, Vec3{0.0f, 1.0f, 0.0f}));
    const Vec3 up = cross(right, fwd);

    Ray ray;
    ray.origin = camera.position();
    ray.dir = normalize(right * viewDir.x + up * viewDir.y + fwd);
    return ray;
}

std::string pickObject(const Scene& scene, const Ray& ray) {
    return pickObjectExcept(scene, ray, {});
}

std::string pickObjectExcept(const Scene& scene, const Ray& ray,
                             const std::vector<std::string>& ignore) {
    std::string name;
    Vec3 point;
    pickObjectHit(scene, ray, ignore, name, point);
    return name;
}

bool pickObjectHit(const Scene& scene, const Ray& ray,
                   const std::vector<std::string>& ignore, std::string& nameOut,
                   Vec3& pointOut) {
    Vec3 normal;
    return pickObjectFace(scene, ray, ignore, nameOut, pointOut, normal);
}

bool pickObjectFace(const Scene& scene, const Ray& ray,
                    const std::vector<std::string>& ignore, std::string& nameOut,
                    Vec3& pointOut, Vec3& normalOut) {
    // t es del espacio local de cada objeto (depende de su escala), asi
    // que se compara la distancia real en mundo al punto de impacto.
    float bestT = FLT_MAX;
    bool found = false;
    for (const SceneObject& object : scene.objects()) {
        // Una part Locked no se puede seleccionar en el viewport (sigue
        // contando como superficie para el snap: eso lo hace rayObjectFace).
        if (object.locked) continue;
        if (std::find(ignore.begin(), ignore.end(), object.name) !=
            ignore.end()) {
            continue;
        }
        Vec3 point;
        Vec3 normal;
        if (!rayObjectFace(object, ray, point, normal)) continue;
        const float worldT = length(point - ray.origin);
        if (worldT < bestT) {
            bestT = worldT;
            nameOut = object.name;
            pointOut = point;
            normalOut = normal;
            found = true;
        }
    }
    return found;
}

bool rayObjectFace(const SceneObject& object, const Ray& ray, Vec3& pointOut,
                   Vec3& normalOut) {
    const Mat4 inv = inverseModel(object);
    const Vec3 localOrigin = affine(inv, ray.origin);
    const Vec3 localDir = transformDir(inv, ray.dir);
    float t = 0.0f;
    Vec3 localNormal;
    if (!rayBox(localOrigin, localDir, t, localNormal)) return false;
    pointOut = affine(object.modelMatrix(), localOrigin + localDir * t);
    normalOut = faceNormalWorld(object, localNormal);
    return true;
}

std::vector<std::string> selectInRect(const Scene& scene, const Mat4& viewProj,
                                      const Vec2& a, const Vec2& b,
                                      int width, int height) {
    const float rx0 = std::fmin(a.x, b.x);
    const float ry0 = std::fmin(a.y, b.y);
    const float rx1 = std::fmax(a.x, b.x);
    const float ry1 = std::fmax(a.y, b.y);

    std::vector<std::string> hits;
    for (const SceneObject& object : scene.objects()) {
        // Las parts Locked quedan fuera del marquee (no se seleccionan).
        if (object.locked) continue;
        const Mat4 mvp = viewProj * object.modelMatrix();
        float minX = FLT_MAX;
        float minY = FLT_MAX;
        float maxX = -FLT_MAX;
        float maxY = -FLT_MAX;
        int corners = 0;
        for (int corner = 0; corner < 8; ++corner) {
            const Vec3 local{(corner & 1) ? 0.5f : -0.5f,
                             (corner & 2) ? 0.5f : -0.5f,
                             (corner & 4) ? 0.5f : -0.5f};
            Vec3 ndc;
            if (!transformPoint(mvp, local, ndc)) continue;
            const Vec2 px = ndcToPixel(ndc, width, height);
            if (px.x < minX) minX = px.x;
            if (px.y < minY) minY = px.y;
            if (px.x > maxX) maxX = px.x;
            if (px.y > maxY) maxY = px.y;
            ++corners;
        }
        if (corners == 0) continue;
        if (maxX < rx0 || minX > rx1 || maxY < ry0 || minY > ry1) continue;
        hits.push_back(object.name);
    }
    return hits;
}

} // namespace sk
