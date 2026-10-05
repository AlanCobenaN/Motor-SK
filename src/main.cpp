#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "core/config.h"
#include "core/log.h"
#include "math/math.h"
#include "platform/window.h"
#include "project/project.h"
#include "render/renderer.h"
#include "scene/camera.h"
#include "ui/code_view.h"
#include "ui/place_view.h"
#include "ui/projects_panel.h"
#include "ui/workspace_tabs.h"

namespace {

int parseFramesArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) {
            return std::atoi(argv[i + 1]);
        }
    }
    return -1;
}

// --proyecto <ruta>: abre un proyecto directamente (para pruebas).
std::string parseProjectArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--proyecto") == 0) {
            return argv[i + 1];
        }
    }
    return "";
}

double nowSeconds() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

} // namespace

int main(int argc, char** argv) {
    // DPI per-monitor v2: sin esto Windows estira la ventana cuando se
    // cambia la escala del escritorio y el borde derecho queda con
    // artefactos. Hay que declararlo antes de crear cualquier ventana.
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDPIAware();
    }

    const int maxFrames = parseFramesArg(argc, argv);
    const std::string startupProject = parseProjectArg(argc, argv);

    // Medal y OBS registran capas implicitas de Vulkan cuyos hooks
    // interceptan la creacion de swapchain y corrompen el heap de este
    // proceso (confirmado con AddressSanitizer). Cada capa declara una
    // variable de deshabilitacion que se lee al crear la instancia.
    _putenv_s("DISABLE_VULKAN_MEDAL_OBS_CAPTURE", "1");
    _putenv_s("DISABLE_VULKAN_OBS_CAPTURE", "1");
    SK_INFO("Capas de captura de Medal/OBS desactivadas");

    sk::Window window;
    if (!window.create(1280, 720, "Motor SK")) {
        return 1;
    }

    sk::Renderer renderer;
    if (!renderer.init(window)) {
        SK_ERROR("No se pudo inicializar el renderer");
        MessageBoxA(static_cast<HWND>(window.nativeHandle()),
                    "No se pudo inicializar Vulkan.\n"
                    "Comprueba que hay un driver o ICD de Vulkan instalado.",
                    "Motor SK", MB_OK | MB_ICONERROR);
        return 1;
    }

    // La carpeta raiz de proyectos se crea sola en el primer arranque.
    sk::project::ensureRootFolder();

    sk::Config config;

    // modo menu: panel Win32 visible; modo vista3d: render + camara.
    bool view3d = false;
    sk::Project active;

    sk::ProjectsPanel panel;
    sk::WorkspaceTabs workspace;
    sk::PlaceView place;
    sk::CodeView code;

    auto openProject = [&](const std::string& folder) {
        sk::Project p;
        if (!sk::project::load(folder, p)) {
            SK_ERROR("No se pudo abrir el proyecto: %s", folder.c_str());
            return;
        }
        config.addRecent(folder);
        config.save();
        active = std::move(p);
        view3d = true;
        panel.setVisible(false);
        workspace.setVisible(true);
        const int tab = workspace.active();
        place.setVisible(tab == 0);
        code.setVisible(tab == 1);
        code.open(active);
        SetWindowTextA(static_cast<HWND>(window.nativeHandle()),
                       ("Motor SK - " + active.name).c_str());
        SK_INFO("Proyecto abierto: %s", active.name.c_str());
    };

    if (!panel.create(window.nativeHandle(), &config, openProject)) {
        renderer.shutdown();
        window.destroy();
        return 1;
    }

    if (!workspace.create(window.nativeHandle())) {
        workspace.destroy();
        panel.destroy();
        renderer.shutdown();
        window.destroy();
        return 1;
    }

    if (!place.create(window.nativeHandle())) {
        place.destroy();
        workspace.destroy();
        panel.destroy();
        renderer.shutdown();
        window.destroy();
        return 1;
    }

    if (!code.create(window.nativeHandle())) {
        code.destroy();
        place.destroy();
        workspace.destroy();
        panel.destroy();
        renderer.shutdown();
        window.destroy();
        return 1;
    }

    // Solo la division PLACE muestra Explorer/Properties y solo CODE
    // muestra el organizador de scripts; GUI vendra despues.
    workspace.setOnTabChanged([&](int tab) {
        place.setVisible(tab == 0);
        code.setVisible(tab == 1);
    });

    if (!startupProject.empty()) {
        // Arranque en modo vista 3D (pruebas/atajos).
        openProject(startupProject);
    }

    sk::Camera camera;
    const double startTime = nowSeconds();
    double lastTime = startTime;
    int frame = 0;
    bool escWasDown = false;

    while (window.pumpEvents() && (maxFrames < 0 || frame < maxFrames)) {
        if (window.consumeResized()) {
            SK_INFO("resize: %dx%d", window.framebufferWidth(), window.framebufferHeight());
            renderer.invalidateSwapchain();
            panel.resize(window.framebufferWidth(), window.framebufferHeight());
            workspace.resize(window.framebufferWidth(), window.framebufferHeight());
            place.resize(window.framebufferWidth(), window.framebufferHeight());
            code.resize(window.framebufferWidth(), window.framebufferHeight());
        }

        if (!view3d) {
            // Menu: solo se procesan mensajes (los controles Win32 pintan
            // encima; el renderer no se llama hasta abrir un proyecto).
            ++frame;
            Sleep(15);
            continue;
        }

        // Esc: volver al panel de proyectos.
        const bool escDown = window.keyDown(VK_ESCAPE);
        if (escDown && !escWasDown) {
            view3d = false;
            panel.setVisible(true);
            panel.refresh();
            workspace.setVisible(false);
            place.setVisible(false);
            code.setVisible(false);
            SetWindowTextA(static_cast<HWND>(window.nativeHandle()), "Motor SK");
            escWasDown = escDown;
            continue;
        }
        escWasDown = escDown;

        const double now = nowSeconds();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f; // proteccion contra picos (ventana arrastrada)

        camera.update(dt, window);

        const float aspect = (window.framebufferHeight() > 0)
            ? static_cast<float>(window.framebufferWidth()) /
              static_cast<float>(window.framebufferHeight())
            : 16.0f / 9.0f;
        const sk::Mat4 projection = sk::perspective(sk::radians(60.0f), aspect, 0.1f, 200.0f);
        const sk::Mat4 viewProj = projection * camera.view();

        if (!renderer.drawFrame(viewProj)) {
            SK_ERROR("drawFrame fallo");
            return 1;
        }

        ++frame;
    }

    code.destroy();
    place.destroy();
    workspace.destroy();
    panel.destroy();
    renderer.shutdown();
    window.destroy();

    const double elapsed = nowSeconds() - startTime;
    SK_INFO("saliendo tras %d frames (%.1fs)", frame, elapsed);
    return 0;
}
