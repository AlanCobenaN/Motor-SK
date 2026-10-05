#pragma once

#include <string>
#include <vector>

namespace sk {

struct Project;

// Organizacion de scripts del proyecto, estilo Roblox simplificado.
//
// Cada proyecto tiene la carpeta scripts/ con cuatro categorias raiz:
//
//   scripts/
//     Server/    logica del servidor
//     Shared/    codigo comun
//     Player/    logica de cliente
//     Character/ logica del personaje
//
// Dentro hay carpetas anidadas y scripts con extension .sk. Los scripts
// todavia no se asocian a objetos de la escena (eso llega con ModelScript).
namespace scripts {

inline constexpr const char* kRoots[] = {"Server", "Shared", "Player", "Character"};
inline constexpr int kRootCount = 4;

// Carpeta scripts/ del proyecto (no la crea).
std::string rootFolder(const Project& p);

// Crea scripts/ con las cuatro carpetas raiz si faltan (idempotente).
bool ensureFolders(const Project& p);

struct Node {
    std::string rel;   // ruta relativa a scripts/ ("" solo como raiz de list)
    std::string name;  // nombre visible (los scripts incluyen .sk)
    bool isDir = false;
};

// Contenido directo de una carpeta relativa; con rel = "" devuelve las
// cuatro categorias raiz. Carpetas primero, todo alfabetico.
std::vector<Node> list(const Project& p, const std::string& rel);

// Primera categoria de una ruta ("Util" -> "Server"; "" si rel es raiz).
std::string rootOf(const std::string& rel);

// true si no se puede borrar/renombrar (vacio o una de las cuatro raices).
bool isProtected(const std::string& rel);

// true si el nombre no es valido para carpeta ni para script.
bool isBadName(const std::string& name);

bool createFolder(const Project& p, const std::string& parentRel,
                  const std::string& name);
// La extension .sk se anade sola si falta.
bool createScript(const Project& p, const std::string& parentRel,
                  const std::string& name);
// newName no lleva extension; si el nodo era script se conserva .sk.
bool renameNode(const Project& p, const std::string& rel, const std::string& newName);
bool removeNode(const Project& p, const std::string& rel);

} // namespace scripts
} // namespace sk
