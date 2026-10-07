#include "meshes.h"

#include <cmath>

namespace sk {

namespace {

constexpr float kPi = 3.14159265358979f;

// Luz direccional fija (arriba, un poco al frente y a la derecha), la
// misma que el cubo traducia a sus seis grises fijos. El tono se
// acota a [0.30, 0.90] para que ninguna cara quede negra ni quemada.
float shade(float nx, float ny, float nz) {
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 0.0f) {
        nx /= len;
        ny /= len;
        nz /= len;
    }
    constexpr float lx = 0.30f;
    constexpr float ly = 0.80f;
    constexpr float lz = 0.52f;
    const float ln = std::sqrt(lx * lx + ly * ly + lz * lz);
    float s = 0.60f + 0.31f * ((nx * lx + ny * ly + nz * lz) / ln);
    if (s < 0.30f) s = 0.30f;
    if (s > 0.90f) s = 0.90f;
    return s;
}

// Tres vertices con el mismo gris calculado de la normal de la cara.
// Todas las mallas estan centradas en el origen, asi que la normal se
// orienta hacia fuera comparando con el centroide: asi el sentido de
// los vertices (indiferente con el cull desactivado) no cambia el tono.
void pushTri(std::vector<MeshVertex>& out, Vec3 a, Vec3 b, Vec3 c) {
    const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const float vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    float nx = uy * vz - uz * vy;
    float ny = uz * vx - ux * vz;
    float nz = ux * vy - uy * vx;
    const float mx = (a.x + b.x + c.x) / 3.0f;
    const float my = (a.y + b.y + c.y) / 3.0f;
    const float mz = (a.z + b.z + c.z) / 3.0f;
    if (nx * mx + ny * my + nz * mz < 0.0f) {
        nx = -nx;
        ny = -ny;
        nz = -nz;
    }
    const float s = shade(nx, ny, nz);
    out.push_back({{a.x, a.y, a.z}, {s, s, s}});
    out.push_back({{b.x, b.y, b.z}, {s, s, s}});
    out.push_back({{c.x, c.y, c.z}, {s, s, s}});
}

void pushQuad(std::vector<MeshVertex>& out, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
    pushTri(out, a, b, c);
    pushTri(out, a, c, d);
}

// Cubo unitario: el original del motor, con sus seis grises fijos por
// cara (se pintan mejor que con la luz generica: no cambia lo que ya
// se veia en las escenas existentes).
void buildCube(std::vector<MeshVertex>& out) {
    constexpr float p = 0.5f;
    const float shades[6] = {0.62f, 0.55f, 0.85f, 0.35f, 0.70f, 0.45f};

    // Cuatro esquinas por cara, recorriendo el contorno (la diagonal
    // del split es v0-v2). Cull desactivado, asi que el sentido no
    // importa.
    const float faces[6][4][3] = {
        {{p, -p, -p}, {p, p, -p}, {p, p, p}, {p, -p, p}},       // +X
        {{-p, -p, p}, {-p, p, p}, {-p, p, -p}, {-p, -p, -p}},   // -X
        {{-p, p, -p}, {-p, p, p}, {p, p, p}, {p, p, -p}},       // +Y
        {{-p, -p, p}, {-p, -p, -p}, {p, -p, -p}, {p, -p, p}},   // -Y
        {{-p, -p, p}, {p, -p, p}, {p, p, p}, {-p, p, p}},       // +Z
        {{p, -p, -p}, {-p, -p, -p}, {-p, p, -p}, {p, p, -p}},   // -Z
    };

    out.reserve(out.size() + 36);
    for (int face = 0; face < 6; ++face) {
        const float s = shades[face];
        const auto push = [&](int corner) {
            const float* v = faces[face][corner];
            out.push_back({{v[0], v[1], v[2]}, {s, s, s}});
        };
        push(0);
        push(1);
        push(2);
        push(0);
        push(2);
        push(3);
    }
}

// Rombo = octaedro: seis vertices opuestos dos a dos y ocho caras
// triangulares (el "diamante" clasico, 24 vertices).
void buildRhombus(std::vector<MeshVertex>& out) {
    const Vec3 top{0.0f, 0.5f, 0.0f};
    const Vec3 bottom{0.0f, -0.5f, 0.0f};
    const Vec3 ring[4] = {{0.5f, 0.0f, 0.0f},
                          {0.0f, 0.0f, 0.5f},
                          {-0.5f, 0.0f, 0.0f},
                          {0.0f, 0.0f, -0.5f}};
    for (int i = 0; i < 4; ++i) {
        const Vec3& a = ring[i];
        const Vec3& b = ring[(i + 1) & 3];
        pushTri(out, top, a, b);
        pushTri(out, bottom, b, a);
    }
}

// Esfera low-poly (UV): 8 longitudes x 4 bandas = 48 triangulos. Los
// polos son un unico vertice, asi que las bandas extremas salen de
// tres en tres.
void buildSphere(std::vector<MeshVertex>& out) {
    constexpr int kSeg = 8;
    constexpr int kBands = 4;
    const auto point = [](int s, int b) {
        const float lat = kPi * (static_cast<float>(b) / kBands - 0.5f);
        const float lon = 2.0f * kPi * static_cast<float>(s) / kSeg;
        const float r = 0.5f * std::cos(lat);
        return Vec3{r * std::cos(lon), 0.5f * std::sin(lat),
                    r * std::sin(lon)};
    };
    out.reserve(out.size() + 144);
    for (int b = 0; b < kBands; ++b) {
        for (int s = 0; s < kSeg; ++s) {
            const Vec3 p00 = point(s, b);
            const Vec3 p01 = point(s + 1, b);
            const Vec3 p10 = point(s, b + 1);
            const Vec3 p11 = point(s + 1, b + 1);
            if (b == 0) {
                pushTri(out, p00, p10, p11); // polo en p00/p01
            } else if (b == kBands - 1) {
                pushTri(out, p00, p01, p10); // polo en p10/p11
            } else {
                pushTri(out, p00, p10, p11);
                pushTri(out, p00, p11, p01);
            }
        }
    }
}

// Cilindro con el eje en X (convencion de Roblox: Size.x es la
// longitud) y radio 0.5 en los ejes Y/Z. Ocho segmentos: 16 triangulos
// de lado + 16 de tapa.
void buildCylinder(std::vector<MeshVertex>& out) {
    constexpr int kSeg = 8;
    const auto ring = [](int s, float x) {
        const float t = 2.0f * kPi * static_cast<float>(s) / kSeg;
        return Vec3{x, 0.5f * std::cos(t), 0.5f * std::sin(t)};
    };
    out.reserve(out.size() + 96);
    for (int s = 0; s < kSeg; ++s) {
        pushTri(out, {0.5f, 0.0f, 0.0f}, ring(s, 0.5f), ring(s + 1, 0.5f));
        pushTri(out, {-0.5f, 0.0f, 0.0f}, ring(s + 1, -0.5f), ring(s, -0.5f));
        pushQuad(out, ring(s, -0.5f), ring(s, 0.5f), ring(s + 1, 0.5f),
                 ring(s + 1, -0.5f));
    }
}

// Cuna: prisma triangular (base cuadrada, cara vertical en la trasera
// z=-0.5 y pendiente que baja hacia +Z). Ocho triangulos.
void buildWedge(std::vector<MeshVertex>& out) {
    const Vec3 a{-0.5f, -0.5f, -0.5f};
    const Vec3 b{0.5f, -0.5f, -0.5f};
    const Vec3 c{-0.5f, -0.5f, 0.5f};
    const Vec3 d{0.5f, -0.5f, 0.5f};
    const Vec3 e{-0.5f, 0.5f, -0.5f};
    const Vec3 f{0.5f, 0.5f, -0.5f};
    pushQuad(out, a, c, d, b);  // base (y=-0.5)
    pushQuad(out, a, b, f, e);  // trasera vertical (z=-0.5)
    pushQuad(out, e, f, d, c);  // pendiente
    pushTri(out, a, e, c);      // lateral -X
    pushTri(out, b, d, f);      // lateral +X
}

// Cuna de esquina: tetraedro de tres triangulos rectos mutuamente
// perpendiculares (arista vertical en la esquina trasera-izquierda) y
// una pendiente que baja hacia la esquina delantera-derecha.
void buildCornerWedge(std::vector<MeshVertex>& out) {
    const Vec3 a{-0.5f, -0.5f, -0.5f}; // pie de la arista vertical
    const Vec3 v{-0.5f, 0.5f, -0.5f};  // cima de la arista vertical
    const Vec3 b{0.5f, -0.5f, -0.5f};  // base hacia +X
    const Vec3 c{-0.5f, -0.5f, 0.5f};  // base hacia +Z
    pushTri(out, a, c, b); // base (y=-0.5)
    pushTri(out, a, b, v); // trasera vertical (z=-0.5)
    pushTri(out, a, v, c); // lateral vertical (x=-0.5)
    pushTri(out, b, c, v); // pendiente
}

} // namespace

void buildShapeMesh(Shape shape, std::vector<MeshVertex>& out) {
    switch (shape) {
        case Shape::Rhombus:
            buildRhombus(out);
            break;
        case Shape::Sphere:
            buildSphere(out);
            break;
        case Shape::Cylinder:
            buildCylinder(out);
            break;
        case Shape::Wedge:
            buildWedge(out);
            break;
        case Shape::CornerWedge:
            buildCornerWedge(out);
            break;
        case Shape::Cube:
        default:
            buildCube(out);
            break;
    }
}

} // namespace sk
