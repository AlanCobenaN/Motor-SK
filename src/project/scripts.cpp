#include "scripts.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "../core/log.h"
#include "project.h"

namespace fs = std::filesystem;

namespace sk {
namespace scripts {

namespace {

// Ruta absoluta de un nodo relativo dentro de scripts/.
fs::path absPath(const Project& p, const std::string& rel) {
    fs::path path = fs::path(rootFolder(p));
    if (!rel.empty()) path /= fs::path(rel);
    return path;
}

// Las rutas vienen del arbol (controladas), pero por si acaso.
bool isSafeRel(const std::string& rel) {
    if (rel.find("..") != std::string::npos) return false;
    if (rel.find(':') != std::string::npos) return false;
    if (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) return false;
    return true;
}

bool isScriptName(const std::string& name) {
    return name.size() > 3 && name.compare(name.size() - 3, 3, ".sk") == 0;
}

Node makeNode(const fs::directory_entry& entry, const std::string& parentRel) {
    Node node;
    node.name = entry.path().filename().string();
    node.rel = parentRel.empty() ? node.name : parentRel + "/" + node.name;
    node.isDir = entry.is_directory();
    return node;
}

} // namespace

std::string rootFolder(const Project& p) {
    return p.folder + "/scripts";
}

bool ensureFolders(const Project& p) {
    std::error_code ec;
    fs::create_directories(fs::path(rootFolder(p)), ec);
    if (ec) {
        SK_ERROR("scripts: no se pudo crear scripts/ (%s)", ec.message().c_str());
        return false;
    }
    for (const char* root : kRoots) {
        fs::create_directories(absPath(p, root), ec);
        if (ec) {
            SK_ERROR("scripts: no se pudo crear %s (%s)", root, ec.message().c_str());
            return false;
        }
    }
    return true;
}

std::vector<Node> list(const Project& p, const std::string& rel) {
    std::vector<Node> nodes;

    if (rel.empty()) {
        for (const char* root : kRoots) {
            nodes.push_back({root, root, true});
        }
        return nodes;
    }

    if (!isSafeRel(rel)) return nodes;

    std::error_code ec;
    const fs::path dir = absPath(p, rel);
    if (!fs::is_directory(dir, ec) || ec) return nodes;

    for (const fs::directory_entry& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_directory()) {
            nodes.push_back(makeNode(entry, rel));
        } else if (entry.is_regular_file() && isScriptName(entry.path().filename().string())) {
            nodes.push_back(makeNode(entry, rel));
        }
    }

    std::sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return a.name < b.name;
    });
    return nodes;
}

std::string rootOf(const std::string& rel) {
    const std::string::size_type slash = rel.find('/');
    if (slash == std::string::npos) return "";
    return rel.substr(0, slash);
}

bool isProtected(const std::string& rel) {
    return rel.empty() || rel.find('/') == std::string::npos;
}

bool isBadName(const std::string& name) {
    if (!project::isValidName(name)) return true;
    if (isScriptName(name)) return true; // las carpetas no terminan en .sk
    return false;
}

bool createFolder(const Project& p, const std::string& parentRel,
                  const std::string& name) {
    if (isBadName(name) || !isSafeRel(parentRel)) return false;

    std::error_code ec;
    const fs::path target = absPath(p, parentRel) / fs::path(name);
    if (fs::exists(target, ec) || ec) return false;
    fs::create_directories(target, ec);
    if (ec) {
        SK_ERROR("scripts: no se pudo crear %s (%s)", name.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

bool createScript(const Project& p, const std::string& parentRel,
                  const std::string& name) {
    std::string base = name;
    if (isScriptName(base)) base.erase(base.size() - 3);
    if (base.empty() || !project::isValidName(base) || !isSafeRel(parentRel)) {
        SK_ERROR("createScript: nombre o carpeta invalidos (%s en %s)",
                 name.c_str(), parentRel.c_str());
        return false;
    }

    std::error_code ec;
    const fs::path target = absPath(p, parentRel) / fs::path(base + ".sk");
    if (fs::exists(target, ec) || ec) {
        SK_ERROR("createScript: ya existe %s", target.string().c_str());
        return false;
    }

    const fs::path parent = absPath(p, parentRel);
    if (!fs::is_directory(parent, ec) || ec) {
        SK_ERROR("createScript: no existe la carpeta %s", parent.string().c_str());
        return false;
    }

    std::ofstream file(target, std::ios::trunc);
    if (!file) {
        SK_ERROR("scripts: no se pudo escribir %s", target.string().c_str());
        return false;
    }
    file << "-- Script nuevo de Motor SK\n";
    return true;
}

bool renameNode(const Project& p, const std::string& rel, const std::string& newName) {
    if (isProtected(rel) || !isSafeRel(rel)) return false;

    const std::string oldName = fs::path(rel).filename().string();
    const bool script = isScriptName(oldName);

    std::string base = newName;
    if (script) {
        if (isScriptName(base)) base.erase(base.size() - 3);
        if (base.empty() || !project::isValidName(base)) return false;
    } else {
        if (isBadName(base)) return false;
    }
    if (base == oldName || (script && base + ".sk" == oldName)) return true;

    const fs::path parent = absPath(p, rel).parent_path();
    const fs::path target = parent / fs::path(script ? base + ".sk" : base);

    std::error_code ec;
    if (fs::exists(target, ec) || ec) return false;
    fs::rename(absPath(p, rel), target, ec);
    if (ec) {
        SK_ERROR("scripts: no se pudo renombrar %s (%s)", rel.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

bool removeNode(const Project& p, const std::string& rel) {
    if (isProtected(rel) || !isSafeRel(rel)) return false;

    std::error_code ec;
    fs::remove_all(absPath(p, rel), ec);
    if (ec) {
        SK_ERROR("scripts: no se pudo borrar %s (%s)", rel.c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

} // namespace scripts
} // namespace sk
