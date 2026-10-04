#pragma once

#include <functional>
#include <string>
#include <vector>

#include "../project/project.h"

namespace sk {

class Config;

// Panel Win32 con la lista de proyectos: un ListView en modo report con
// las columnas Nombre / Ultima apertura / Ruta, mas los botones
// Nuevo, Renombrar, Borrar y Abrir.
//
// Es una ventana hija con wndProc propia, asi que los WM_COMMAND/WM_NOTIFY
// de sus controles no llegan a la ventana principal.
class ProjectsPanel {
public:
    using OpenHandler = std::function<void(const std::string& folder)>;

    bool create(void* parentHwnd, Config* config, OpenHandler onOpen);
    void destroy();

    void setVisible(bool visible);
    bool visible() const { return visible_; }

    // Reescanea la carpeta raiz y repuebla la lista.
    void refresh();

    // Llamar cuando la ventana principal cambia de tamano.
    void resize(int width, int height);

private:
    static long long __stdcall wndProc(void* hwnd, unsigned int msg,
                                       unsigned long long wParam, long long lParam);

    // Pintado custom (NM_CUSTOMDRAW) de filas y columnas del ListView.
    long long customDraw(void* nm);

    // Esquinas redondeadas del ListView (region Win32).
    void applyListRegion();

    int selectedIndex() const;
    const Project* selectedProject() const;

    void onSelectionChanged();
    void openSelected();
    void newProject();
    void renameSelected();
    void deleteSelected();
    void updateButtons();

    void* parent_ = nullptr;
    void* hwnd_ = nullptr;
    void* list_ = nullptr;
    void* btnNew_ = nullptr;
    void* btnRename_ = nullptr;
    void* btnDelete_ = nullptr;
    void* btnOpen_ = nullptr;

    Config* config_ = nullptr;
    OpenHandler onOpen_;
    std::vector<Project> projects_;
    bool visible_ = false;
};

} // namespace sk
