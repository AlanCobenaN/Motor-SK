#pragma once

#include <string>
#include <vector>

namespace sk {

// Preferencias simples guardadas en %APPDATA%\MotorSK\config.txt
// (formato "clave: valor", una por linea). Ahora solo los proyectos
// recientes: "reciente: <epoch>|<ruta>", maximo 5, mas reciente primero.
class Config {
public:
    struct Recent {
        long long openedAt = 0; // segundos desde epoch (0 = desconocido)
        std::string folder;
    };

    Config(); // intenta cargar el archivo si existe

    const std::vector<Recent>& recents() const { return recents_; }

    // Pone la carpeta la primera, sin duplicados, y recorta a 5.
    void addRecent(const std::string& folder);
    void removeRecent(const std::string& folder);

    bool save() const;

private:
    void load();

    std::string path_;
    std::vector<Recent> recents_;
};

} // namespace sk
