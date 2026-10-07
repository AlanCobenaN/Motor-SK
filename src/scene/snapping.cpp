#include "snapping.h"

#include <algorithm>
#include <cmath>

#include "scene.h"

namespace sk {

namespace {

// Rotacion de un objeto: R = Rx * Ry * Rz (el mismo orden que usa
// SceneObject::modelMatrix para componer su matriz).
Mat4 rotXYZ(const Vec3& degrees) {
    return rotateX(degrees.x) * rotateY(degrees.y) * rotateZ(degrees.z);
}

// Traspuesta (3x3) de una rotacion = su inversa.
Mat4 rotTranspose(const Mat4& m) {
    Mat4 r;
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) r.at(col, row) = m.at(row, col);
    }
    return r;
}

// Euler (grados) que reproduce R = Rx(a)*Ry(b)*Rz(c). Ojo: en este
// repo rotateX va con el signo opuesto al resto (Rx(a) es Rx_std(-a)),
// asi que el producto desarrolla a:
//   M00=cb*cc   M01=-cb*sc  M02=sb
//   M10=ca*sc-sa*sb*cc  M11=ca*cc+sa*sb*sc  M12=sa*cb
//   M20=-sa*sc-ca*sb*cc M21=-sa*cc+ca*sb*sc M22=ca*cb
// De ahi: b = asin(M02), a = atan2(M12, M22), c = atan2(-M01, M00).
// Con b = +-90° hay bloqueo gimbalico (c se pierde): se fija c = 0 y el
// acople restante de a depende del signo de sb.
Vec3 eulerFromRot(const Mat4& m) {
    const auto E = [&](int row, int col) { return m.at(col, row); };
    constexpr float kToDeg = 180.0f / kPi;
    const float sb = std::clamp(E(0, 2), -1.0f, 1.0f);
    const float cb = std::sqrt(std::fmax(0.0f, 1.0f - sb * sb));
    if (cb > 1e-5f) {
        return {std::atan2(E(1, 2), E(2, 2)) * kToDeg, std::asin(sb) * kToDeg,
                std::atan2(-E(0, 1), E(0, 0)) * kToDeg};
    }
    const float coupled = std::atan2(E(1, 0), E(1, 1));
    const float a = sb > 0.0f ? -coupled : coupled;
    return {a * kToDeg, sb > 0.0f ? 90.0f : -90.0f, 0.0f};
}

Vec3 axisVec(int axis) {
    Vec3 v{};
    (&v.x)[axis] = 1.0f;
    return v;
}

// Rotacion (Rodrigues) minima que lleva la direccion `from` a `to`.
Mat4 rotationBetween(const Vec3& from, const Vec3& to) {
    const float d = std::clamp(dot(from, to), -1.0f, 1.0f);
    const Vec3 cr = cross(from, to);
    const float s = length(cr);
    Vec3 axis{};
    float angle = 0.0f;
    if (s < 1e-6f) {
        if (d > 0.0f) return Mat4::identity(); // mismas direcciones
        // Opuestas (180°): el eje cruzado se anula, se usa cualquiera
        // perpendicular a `from`.
        axis = std::fabs(from.x) < 0.9f
                   ? normalize(cross(from, Vec3{1.0f, 0.0f, 0.0f}))
                   : normalize(cross(from, Vec3{0.0f, 1.0f, 0.0f}));
        angle = kPi;
    } else {
        axis = cr * (1.0f / s);
        angle = std::atan2(s, d);
    }

    // R = c*I + (1-c)*u u^T + s*[u]x, con u unitario.
    const float c = std::cos(angle);
    const float sn = std::sin(angle);
    const float ic = 1.0f - c;
    const float xy = ic * axis.x * axis.y;
    const float xz = ic * axis.x * axis.z;
    const float yz = ic * axis.y * axis.z;
    const float xs = sn * axis.x;
    const float ys = sn * axis.y;
    const float zs = sn * axis.z;
    Mat4 r = Mat4::identity();
    r.at(0, 0) = c + ic * axis.x * axis.x;
    r.at(1, 0) = xy - zs;
    r.at(2, 0) = xz + ys;
    r.at(0, 1) = xy + zs;
    r.at(1, 1) = c + ic * axis.y * axis.y;
    r.at(2, 1) = yz - xs;
    r.at(0, 2) = xz - ys;
    r.at(1, 2) = yz + xs;
    r.at(2, 2) = c + ic * axis.z * axis.z;
    return r;
}

// Media caja (soporte) de un objeto con escala `boxScale` y rotacion
// `rot` en la direccion `dir`: distancia del centro a esa cara.
float supportAlong(const Vec3& boxScale, const Mat4& rot, const Vec3& dir) {
    float sum = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const Vec3 col = transformDir(rot, axisVec(i));
        sum += std::fabs(dot(dir, col)) * std::fabs((&boxScale.x)[i]) * 0.5f;
    }
    return sum;
}

float snapTo(float value, float grid) {
    return std::round(value / grid) * grid;
}

} // namespace

void computeSurfaceDrag(const Scene& scene, const Ray& ray0, const Ray& ray1,
                        const std::vector<std::string>& dragged, int leadIndex,
                        const Vec3& grabOffset,
                        const std::vector<DragPose>& startPose,
                        bool alignSurface, float grid, DragResult& out) {
    out.onSurface = false;
    out.surfaceName.clear();
    out.surfaceNormal = {};
    out.groundHit = false;
    out.poses = startPose;

    if (dragged.empty() || startPose.size() != dragged.size() || leadIndex < 0 ||
        leadIndex >= static_cast<int>(dragged.size())) {
        return;
    }
    const SceneObject* lead = scene.findByName(dragged[leadIndex]);
    if (!lead) return;

    const Mat4 R0 = rotXYZ(startPose[leadIndex].rotation);
    Mat4 Rnew = R0;
    Vec3 leadPos = startPose[leadIndex].position;

    // Cara de otro parte bajo el puntero (los arrastrados se ignoran).
    std::string targetName;
    Vec3 hitPoint{};
    Vec3 hitNormal{};
    const SceneObject* target = nullptr;
    if (pickObjectFace(scene, ray1, dragged, targetName, hitPoint, hitNormal)) {
        target = scene.findByName(targetName);
    }

    if (target) {
        // --- Surface snapping: la caja del lead se apoya en la cara ---
        out.onSurface = true;
        out.surfaceName = target->name;
        out.surfaceNormal = hitNormal;

        const Mat4 Rt = rotXYZ(target->rotation);
        // La cara golpeada, en coordenadas locales del destino: el eje
        // dominante de Rt^T * n (su normal local es +-eje).
        const Vec3 localN = transformDir(rotTranspose(Rt), hitNormal);
        int targetAxis = 0;
        float best = 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float comp = std::fabs((&localN.x)[i]);
            if (comp > best) {
                best = comp;
                targetAxis = i;
            }
        }
        const float targetSign = (&localN.x)[targetAxis] >= 0.0f ? 1.0f : -1.0f;

        if (alignSurface) {
            // Cara del lead cuya normal en mundo mira mas al revés que la
            // del destino: esa es la que se apoya. Despues de alinear,
            // su normal en mundo sera exactamente -hitNormal.
            int leadAxis = 0;
            float leadSign = 1.0f;
            float bestDot = 1e9f;
            for (int i = 0; i < 3; ++i) {
                for (int signIndex = 0; signIndex < 2; ++signIndex) {
                    const float sign = signIndex == 0 ? 1.0f : -1.0f;
                    const float d =
                        dot(transformDir(R0, axisVec(i) * sign), hitNormal);
                    if (d < bestDot) {
                        bestDot = d;
                        leadAxis = i;
                        leadSign = sign;
                    }
                }
            }
            // R' = Rt * M, con M llevando la cara del lead a la cara
            // interna del destino (rotacion minima: conserva el resto).
            Rnew = Rt * rotationBetween(axisVec(leadAxis) * leadSign,
                                        axisVec(targetAxis) * (-targetSign));
        }

        // Tangentes de la cara: las otras dos columnas de Rt (ortogonales
        // a la normal, definen el plano de la cara).
        const Vec3 t1 = transformDir(Rt, axisVec((targetAxis + 1) % 3));
        const Vec3 t2 = transformDir(Rt, axisVec((targetAxis + 2) % 3));

        // La cara de contacto queda a ras (soporte en la direccion de la
        // normal) y el punto de agarre cae sobre el punto de golpe.
        const float height = supportAlong(lead->scale, Rnew, hitNormal);
        const Mat4 delta = Rnew * rotTranspose(R0);
        const Vec3 grab = transformDir(delta, grabOffset);
        Vec3 pos = hitPoint + hitNormal * height -
                   (grab - hitNormal * dot(grab, hitNormal));

        // Rejilla en el plano de la cara relativa al centro del destino
        // (asi las caras siguen alineadas aunque el destino este fuera
        // de la rejilla de mundo). La normal no se toca: el ras es exacto.
        if (grid > 0.0f) {
            const Vec3 rel = pos - target->position;
            pos = target->position + t1 * snapTo(dot(rel, t1), grid) +
                  t2 * snapTo(dot(rel, t2), grid) +
                  hitNormal * dot(rel, hitNormal);
        }
        leadPos = pos;
    } else {
        // --- Sin superficie: el rayo cae al plano del suelo Y=0 ---
        Vec3 point{};
        bool grounded = false;
        if (std::fabs(ray1.dir.y) > 1e-6f) {
            const float t = -ray1.origin.y / ray1.dir.y;
            if (t > 0.0f) {
                point = ray1.origin + ray1.dir * t;
                grounded = true;
            }
        }
        out.groundHit = grounded;
        if (!grounded) {
            // Mirando al cielo: el punto de agarre sigue al puntero en
            // el plano de camara que pasa por el (fallback para que el
            // parte no se quede congelado).
            const Vec3 planeN = normalize(ray0.dir);
            const Vec3 anchor = startPose[leadIndex].position + grabOffset;
            point = anchor;
            const float denom = dot(ray1.dir, planeN);
            if (std::fabs(denom) > 1e-6f) {
                const float t = dot(anchor - ray1.origin, planeN) / denom;
                if (t > 0.0f) point = ray1.origin + ray1.dir * t;
            }
        }
        // No hay cara a la que alinear: la rotacion no cambia y la caja
        // inferior queda apoyada en el suelo.
        const float height =
            supportAlong(lead->scale, Rnew, Vec3{0.0f, 1.0f, 0.0f});
        Vec3 pos = point - grabOffset;
        pos.y = height;
        if (grid > 0.0f) {
            pos.x = snapTo(pos.x, grid);
            pos.z = snapTo(pos.z, grid);
        }
        leadPos = pos;
    }

    // El resto del grupo se mantiene rigido alrededor del lead.
    const Mat4 delta = Rnew * rotTranspose(R0);
    for (size_t i = 0; i < dragged.size(); ++i) {
        DragPose& pose = out.poses[i];
        if (static_cast<int>(i) == leadIndex) {
            pose.position = leadPos;
            pose.rotation = eulerFromRot(Rnew);
            continue;
        }
        const Vec3 rel = startPose[i].position - startPose[leadIndex].position;
        pose.position = leadPos + transformDir(delta, rel);
        pose.rotation = eulerFromRot(delta * rotXYZ(startPose[i].rotation));
    }
}

} // namespace sk
