#include "gizmo.h"

#include <algorithm>
#include <cmath>

#include "scene.h"

namespace sk {

namespace {

// Colores de los ejes, distintos de la rejilla (X 0.85/0.25/0.25,
// Y 0.30/0.90/0.35, Z 0.30/0.45/0.95) para distinguirlos por pixel.
const Vec3 kAxisColors[3] = {
    {0.90f, 0.24f, 0.24f}, // X rojo
    {0.27f, 0.90f, 0.35f}, // Y verde
    {0.27f, 0.55f, 1.0f},  // Z azul
};
const Vec3 kAxisDirs[3] = {
    {1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 1.0f},
};
// Celeste de seleccion para el circulo central (sin eje).
const Vec3 kCenterColor{0.49f, 0.82f, 1.0f};

constexpr float kHitThresholdPx = 10.0f;
constexpr int kCircleSegments = 64;
constexpr int kCenterSegments = 20;
constexpr float kTwoPi = 6.28318530718f;

Vec3 axisColor(int axis, int hoveredAxis) {
    if (axis < 0 || axis > 2) return kCenterColor;
    const Vec3 c = kAxisColors[axis];
    if (axis != hoveredAxis) return c;
    // Hover: 50% mezclado con blanco.
    return {(c.x + 1.0f) * 0.5f, (c.y + 1.0f) * 0.5f, (c.z + 1.0f) * 0.5f};
}

Vec3 safeViewDir(const Vec3& v) {
    return (length(v) > 1e-4f) ? normalize(v) : Vec3{0.0f, 0.0f, -1.0f};
}

// Base (u, v) del plano perpendicular al eje con cross(u, v) == eje,
// para que el angulo medido crezca segun la regla de la mano derecha.
void circleBasis(const Vec3& eje, Vec3& u, Vec3& v) {
    const Vec3 ref = (std::fabs(eje.y) < 0.9f) ? Vec3{0.0f, 1.0f, 0.0f}
                                               : Vec3{0.0f, 0.0f, 1.0f};
    u = normalize(cross(ref, eje));
    v = cross(eje, u);
}

// Dos vectores ortogonales entre si y al viewDir (plano de pantalla),
// para circulos/cuadrados que miran a la camara.
void billboardBasis(const Vec3& vd, Vec3& e1, Vec3& e2) {
    const Vec3 ref = (std::fabs(vd.y) < 0.9f) ? Vec3{0.0f, 1.0f, 0.0f}
                                              : Vec3{0.0f, 0.0f, 1.0f};
    e1 = normalize(cross(ref, vd));
    e2 = normalize(cross(vd, e1));
}

float component(const Vec3& v, int i) {
    return (i == 0) ? v.x : (i == 1) ? v.y : v.z;
}

void setComponent(Vec3& v, int i, float value) {
    if (i == 0) v.x = value;
    else if (i == 1) v.y = value;
    else v.z = value;
}

float normalizeDegrees(float deg) {
    while (deg > 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

float pointSegmentDistance(const Vec2& p, const Vec2& a, const Vec2& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    float t = 0.0f;
    if (len2 > 1e-6f) {
        t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
        t = std::max(0.0f, std::min(1.0f, t));
    }
    const float ex = a.x + t * dx - p.x;
    const float ey = a.y + t * dy - p.y;
    return std::sqrt(ex * ex + ey * ey);
}

// Interseccion del rayo con el plano (punto `point`, normal `n`).
bool rayPlane(const Ray& ray, const Vec3& point, const Vec3& n, Vec3& out) {
    const float denom = dot(ray.dir, n);
    if (std::fabs(denom) < 1e-6f) return false;
    const float t = dot(point - ray.origin, n) / denom;
    out = ray.origin + ray.dir * t;
    return true;
}

} // namespace

std::vector<GizmoLine> buildGizmo(int tool, const Vec3& origin, float scaleMax,
                                  const Vec3& viewDir, int hoveredAxis) {
    std::vector<GizmoLine> lines;
    const float L = 0.5f * std::fabs(scaleMax) + 1.0f;
    const Vec3 vd = safeViewDir(viewDir);

    const auto add = [&](const Vec3& a, const Vec3& b, int axis) {
        lines.push_back({a, b, axisColor(axis, hoveredAxis), axis});
    };

    if (tool == 3) {
        // Rotar: tres circulos de radio 0.75L en los planos perpendiculares
        // a cada eje (sin billboard: giran con el mundo, no con la camara).
        const float R = 0.75f * L;
        for (int axis = 0; axis < 3; ++axis) {
            Vec3 u, v;
            circleBasis(kAxisDirs[axis], u, v);
            Vec3 prev = origin + u * R;
            for (int i = 1; i <= kCircleSegments; ++i) {
                const float a = kTwoPi * (static_cast<float>(i) / kCircleSegments);
                const Vec3 p = origin + (u * std::cos(a) + v * std::sin(a)) * R;
                add(prev, p, axis);
                prev = p;
            }
        }
        return lines;
    }

    // Mover / Escalar: un eje con punta por cada direccion.
    for (int axis = 0; axis < 3; ++axis) {
        const Vec3 eje = kAxisDirs[axis];
        const Vec3 tip = origin + eje * L;
        add(origin, tip, axis);
        if (tool == 1) {
            // Punta de flecha en V, billboard (mirando a la camara).
            Vec3 side = cross(vd, eje);
            if (length(side) < 1e-4f) side = cross(Vec3{0.0f, 0.0f, 1.0f}, eje);
            if (length(side) < 1e-4f) side = cross(Vec3{1.0f, 0.0f, 0.0f}, eje);
            side = normalize(side);
            const Vec3 back = tip - eje * (0.22f * L);
            add(tip, back + side * (0.10f * L), axis);
            add(tip, back - side * (0.10f * L), axis);
        } else {
            // Escalar: cuadrito billboard en el extremo.
            Vec3 e1, e2;
            billboardBasis(vd, e1, e2);
            const float h = 0.08f * L;
            const Vec3 corners[4] = {
                tip + (e1 + e2) * h,
                tip + (e1 - e2) * h,
                tip - (e1 + e2) * h,
                tip + (e2 - e1) * h,
            };
            for (int i = 0; i < 4; ++i) {
                add(corners[i], corners[(i + 1) % 4], axis);
            }
        }
    }

    // Circulo central (adorno, sin eje: no se le puede hacer hit).
    {
        Vec3 e1, e2;
        billboardBasis(vd, e1, e2);
        const float r = 0.12f * L;
        Vec3 prev = origin + e1 * r;
        for (int i = 1; i <= kCenterSegments; ++i) {
            const float a = kTwoPi * (static_cast<float>(i) / kCenterSegments);
            const Vec3 p = origin + (e1 * std::cos(a) + e2 * std::sin(a)) * r;
            lines.push_back({prev, p, kCenterColor, -1});
            prev = p;
        }
    }
    return lines;
}

int hitTestGizmo(const std::vector<GizmoLine>& lines, const Vec2& pointer,
                 const Mat4& viewProj, int width, int height) {
    int bestAxis = -1;
    float bestDist = kHitThresholdPx;
    for (const GizmoLine& line : lines) {
        if (line.axis < 0) continue;
        Vec3 ndcA, ndcB;
        if (!transformPoint(viewProj, line.a, ndcA)) continue;
        if (!transformPoint(viewProj, line.b, ndcB)) continue;
        const Vec2 a{(ndcA.x * 0.5f + 0.5f) * static_cast<float>(width),
                     (ndcA.y * 0.5f + 0.5f) * static_cast<float>(height)};
        const Vec2 b{(ndcB.x * 0.5f + 0.5f) * static_cast<float>(width),
                     (ndcB.y * 0.5f + 0.5f) * static_cast<float>(height)};
        const float d = pointSegmentDistance(pointer, a, b);
        if (d <= kHitThresholdPx && d < bestDist) {
            bestDist = d;
            bestAxis = line.axis;
        }
    }
    return bestAxis;
}

Vec3 gizmoHandlePoint(int tool, int axis, const Vec3& origin, float scaleMax) {
    if (axis < 0 || axis > 2) return origin;
    const float L = 0.5f * std::fabs(scaleMax) + 1.0f;
    if (tool == 3) {
        // Punto del circulo a 45 grados (los ejes cardinales los comparten
        // los tres circulos y el hit test seria ambiguo).
        Vec3 u, v;
        circleBasis(kAxisDirs[axis], u, v);
        const float R = 0.75f * L;
        return origin + (u + v) * (R * 0.70710678f);
    }
    return origin + kAxisDirs[axis] * L;
}

bool gizmoBegin(GizmoDrag& drag, int tool, int axis, const Scene& scene,
                const std::vector<std::string>& selection, const Vec3& origin,
                float scaleMax, const Vec3& viewDir, const Ray& ray) {
    if (selection.empty() || axis < 0 || axis > 2 || tool < 1 || tool > 3) {
        return false;
    }

    GizmoDrag d;
    d.active = true;
    d.tool = tool;
    d.axis = axis;
    d.origin = origin;
    d.axisDir = kAxisDirs[axis];
    d.L = 0.5f * std::fabs(scaleMax) + 1.0f;
    d.snapshots.reserve(selection.size());
    for (const std::string& name : selection) {
        const SceneObject* object = scene.findByName(name);
        if (!object) continue;
        GizmoSnapshot snap;
        snap.name = object->name;
        snap.position = object->position;
        snap.rotation = object->rotation;
        snap.scale = object->scale;
        d.snapshots.push_back(std::move(snap));
    }
    if (d.snapshots.empty()) return false;

    const Vec3 vd = safeViewDir(viewDir);
    if (tool == 3) {
        // Rotar: el plano del circulo (normal = eje).
        d.planeN = d.axisDir;
    } else {
        // Mover/Escalar: plano que contiene el eje y mira a la camara.
        Vec3 n = vd - d.axisDir * dot(vd, d.axisDir);
        if (length(n) < 1e-4f) {
            // Mirando de canto al eje: cualquier normal perpendicular sirve.
            const Vec3 ref =
                (std::fabs(d.axisDir.y) < 0.9f) ? Vec3{0.0f, 1.0f, 0.0f}
                                                : Vec3{0.0f, 0.0f, 1.0f};
            n = cross(d.axisDir, ref);
        }
        d.planeN = normalize(n);
    }

    Vec3 hit;
    if (!rayPlane(ray, d.origin, d.planeN, hit)) return false;
    if (tool == 3) {
        Vec3 u, v;
        circleBasis(d.axisDir, u, v);
        const Vec3 rel = hit - d.origin;
        d.s0 = std::atan2(dot(rel, v), dot(rel, u));
    } else {
        d.s0 = dot(hit - d.origin, d.axisDir);
    }

    drag = std::move(d);
    return true;
}

bool gizmoUpdate(GizmoDrag& drag, const Ray& ray, Scene& scene) {
    if (!drag.active) return false;

    Vec3 hit;
    if (!rayPlane(ray, drag.origin, drag.planeN, hit)) return false;

    if (drag.tool == 1) {
        // Mover: delta = eje * (s - s0), igual para toda la seleccion.
        const float s = dot(hit - drag.origin, drag.axisDir);
        const Vec3 delta = drag.axisDir * (s - drag.s0);
        for (const GizmoSnapshot& snap : drag.snapshots) {
            SceneObject* object = scene.findByName(snap.name);
            if (object) object->position = snap.position + delta;
        }
        return true;
    }

    if (drag.tool == 2) {
        // Escalar: factor sobre la componente del eje arrastrado.
        const float s = dot(hit - drag.origin, drag.axisDir);
        float factor = 1.0f + (s - drag.s0) / drag.L;
        factor = std::max(0.05f, std::min(1000.0f, factor));
        for (const GizmoSnapshot& snap : drag.snapshots) {
            SceneObject* object = scene.findByName(snap.name);
            if (!object) continue;
            object->scale = snap.scale;
            setComponent(object->scale, drag.axis,
                         component(snap.scale, drag.axis) * factor);
        }
        return true;
    }

    // Rotar: el delta de angulo alrededor del eje propio de cada objeto
    // (la posicion no cambia: gira en su centro).
    Vec3 u, v;
    circleBasis(drag.axisDir, u, v);
    const Vec3 rel = hit - drag.origin;
    const float angle = std::atan2(dot(rel, v), dot(rel, u));
    float delta = angle - drag.s0;
    while (delta > kPi) delta -= kTwoPi;
    while (delta < -kPi) delta += kTwoPi;
    const float deltaDeg = delta * (180.0f / kPi);
    for (const GizmoSnapshot& snap : drag.snapshots) {
        SceneObject* object = scene.findByName(snap.name);
        if (!object) continue;
        object->rotation = snap.rotation;
        setComponent(object->rotation, drag.axis,
                     normalizeDegrees(component(snap.rotation, drag.axis) +
                                      deltaDeg));
    }
    return true;
}

void gizmoRevert(GizmoDrag& drag, Scene& scene) {
    for (const GizmoSnapshot& snap : drag.snapshots) {
        SceneObject* object = scene.findByName(snap.name);
        if (!object) continue;
        object->position = snap.position;
        object->rotation = snap.rotation;
        object->scale = snap.scale;
    }
}

} // namespace sk
