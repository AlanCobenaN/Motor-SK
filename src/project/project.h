#pragma once

#include <string>
#include <vector>

namespace sk {

struct Project {
    std::string name;    // campo "nombre" del proyecto.sk (visible)
    std::string folder;  // ruta absoluta de la carpeta del proyecto
    std::string scene;   // ruta de la escena inicial (relativa a folder)
    std::string version; // campo "version" del proyecto.sk
};

// Gestion de proyectos: carpeta raiz, archivo proyecto.sk y CRUD de carpetas.
//
// Un proyecto es una carpeta dentro de la raiz que contiene proyecto.sk:
//
//   nombre: Mi Juego
//   version: 1
//   escena: escenas/inicio.scene
//
// El formato es texto "clave: valor", legible y versionable.
namespace project {

// Documentos\Motor SK\Proyectos (se crea si no existe).
std::string rootFolder();
bool ensureRootFolder();

// Subcarpetas con proyecto.sk, ordenadas por nombre.
std::vector<Project> scan();

// Lee proyecto.sk de una carpeta. Devuelve false si no es un proyecto valido.
bool load(const std::string& folder, Project& out);

// Crea carpeta + proyecto.sk + escenas/mallas/texturas + escena inicial vacia.
bool create(const std::string& name, std::string& outFolder);

// Renombra la carpeta y actualiza el campo "nombre" de proyecto.sk.
bool rename(const std::string& folder, const std::string& newName,
            std::string& outFolder);

// Borra la carpeta completa (irreversible). Solo si contiene proyecto.sk.
bool remove(const std::string& folder);

// Nombre utilizable como carpeta en Windows (sin caracteres reservados).
bool isValidName(const std::string& name);

} // namespace project
} // namespace sk
