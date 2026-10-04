#include "project.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "../core/log.h"

namespace fs = std::filesystem;

namespace sk {
namespace project {

namespace {

const char* kFileName = "proyecto.sk";

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) return "";
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

} // namespace

std::string rootFolder() {
    char base[MAX_PATH]{};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr,
                                SHGFP_TYPE_CURRENT, base))) {
        return "Proyectos";
    }
    return std::string(base) + "\\Motor SK\\Proyectos";
}

bool ensureRootFolder() {
    std::error_code ec;
    fs::create_directories(rootFolder(), ec);
    if (ec) {
        SK_ERROR("No se pudo crear la carpeta de proyectos (%s): %s",
                 rootFolder().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

bool load(const std::string& folder, Project& out) {
    std::ifstream file(fs::path(folder) / kFileName);
    if (!file) return false;

    Project p;
    p.folder = folder;
    p.name = fs::path(folder).filename().string();

    std::string line;
    while (std::getline(file, line)) {
        const auto sep = line.find(':');
        if (sep == std::string::npos) continue;
        const std::string key = trim(line.substr(0, sep));
        const std::string value = trim(line.substr(sep + 1));
        if (key == "nombre") p.name = value;
        else if (key == "version") p.version = value;
        else if (key == "escena") p.scene = value;
    }

    if (p.name.empty()) return false;
    out = std::move(p);
    return true;
}

std::vector<Project> scan() {
    std::vector<Project> result;
    std::error_code ec;

    const std::string root = rootFolder();
    fs::directory_iterator it(root, ec);
    if (ec) return result;

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        std::error_code entryEc;
        if (!entry.is_directory(entryEc)) continue;
        Project p;
        if (load(entry.path().string(), p)) {
            result.push_back(std::move(p));
        }
    }

    std::sort(result.begin(), result.end(),
              [](const Project& a, const Project& b) { return a.name < b.name; });
    return result;
}

bool isValidName(const std::string& name) {
    if (name.empty() || name.size() > 150) return false;
    if (name == "." || name == "..") return false;
    if (name.front() == ' ' || name.back() == ' ') return false;
    for (char c : name) {
        if (c == '\0' || std::strchr("\\/:*?\"<>|", c)) return false;
    }
    return true;
}

bool create(const std::string& name, std::string& outFolder) {
    if (!isValidName(name)) return false;

    const fs::path folder = fs::path(rootFolder()) / name;
    std::error_code ec;
    if (fs::exists(folder, ec)) return false;

    for (const char* sub : {"escenas", "mallas", "texturas"}) {
        fs::create_directories(folder / sub, ec);
        if (ec) {
            SK_ERROR("create: fallo creando %s (%s)", sub, ec.message().c_str());
            return false;
        }
    }

    {
        std::ofstream file(folder / kFileName);
        if (!file) return false;
        file << "nombre: " << name << "\n"
             << "version: 1\n"
             << "escena: escenas/inicio.scene\n";
    }
    {
        std::ofstream file(folder / "escenas" / "inicio.scene");
        if (!file) return false;
    }

    outFolder = folder.string();
    SK_INFO("Proyecto creado: %s", outFolder.c_str());
    return true;
}

bool rename(const std::string& folder, const std::string& newName,
            std::string& outFolder) {
    if (!isValidName(newName)) return false;

    const fs::path oldPath(folder);
    Project current;
    if (!load(folder, current)) return false;

    const fs::path newPath = oldPath.parent_path() / newName;
    if (newPath == oldPath) {
        outFolder = folder;
        return true;
    }

    std::error_code ec;
    if (fs::exists(newPath, ec)) return false;
    fs::rename(oldPath, newPath, ec);
    if (ec) {
        SK_ERROR("rename: fallo renombrando carpeta (%s)", ec.message().c_str());
        return false;
    }

    {
        std::ofstream file(newPath / kFileName);
        if (!file) return false;
        file << "nombre: " << newName << "\n"
             << "version: " << (current.version.empty() ? "1" : current.version)
             << "\n"
             << "escena: "
             << (current.scene.empty() ? "escenas/inicio.scene" : current.scene)
             << "\n";
    }

    outFolder = newPath.string();
    SK_INFO("Proyecto renombrado: %s", outFolder.c_str());
    return true;
}

bool remove(const std::string& folder) {
    // Salvaguarda: solo borramos carpetas que son proyectos.
    Project probe;
    if (!load(folder, probe)) return false;

    std::error_code ec;
    fs::remove_all(folder, ec);
    if (ec) {
        SK_ERROR("remove: fallo borrando %s (%s)", folder.c_str(),
                 ec.message().c_str());
        return false;
    }
    SK_INFO("Proyecto borrado: %s", folder.c_str());
    return true;
}

} // namespace project
} // namespace sk
