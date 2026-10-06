#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/log.h"
#include "math/math.h"
#include "platform/window.h"
#include "project/project.h"
#include "render/renderer.h"
#include "scene/camera.h"
#include "scene/picking.h"
#include "scene/scene.h"
#include "ui/code_view.h"
#include "ui/place_view.h"
#include "ui/projects_panel.h"
#include "ui/script_picker.h"
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
    sk::Scene scene;   // objetos Part de la division PLACE

    sk::ProjectsPanel panel;
    sk::WorkspaceTabs workspace;
    sk::PlaceView place;
    sk::CodeView code;

    // Seleccion activa (nombres en la escena). "primary" es el ultimo
    // objeto tocado: el que el Explorer marca con el caret.
    std::vector<std::string> selection;
    std::string primary;
    // Log de posiciones en pantalla: al cambiar la escena, para que las
    // pruebas sepan donde hacer click.
    bool logProjection = false;

    // Ruta absoluta de la escena del proyecto (escenas/inicio.scene).
    auto scenePath = [](const sk::Project& p) {
        return p.folder + "/" + p.scene;
    };
    auto saveScene = [&]() {
        if (active.folder.empty()) return;
        const std::string path = scenePath(active);
        if (!scene.saveToFile(path)) {
            SK_ERROR("No se pudo guardar la escena: %s", path.c_str());
        }
    };

    auto openProject = [&](const std::string& folder) {
        sk::Project p;
        if (!sk::project::load(folder, p)) {
            SK_ERROR("No se pudo abrir el proyecto: %s", folder.c_str());
            return;
        }
        config.addRecent(folder);
        config.save();
        active = std::move(p);
        scene.clear();
        scene.loadFromFile(scenePath(active));
        place.clearObjects();   // Explorer vuelve a solo dimension01
        for (const sk::SceneObject& object : scene.objects()) {
            place.addObject(object.name); // la ultima queda seleccionada
        }
        view3d = true;
        panel.setVisible(false);
        workspace.setVisible(true);
        const int tab = workspace.active();
        place.setVisible(tab == 0);
        code.setVisible(tab == 1);
        code.open(active);
        SetWindowTextA(static_cast<HWND>(window.nativeHandle()),
                       ("Motor SK - " + active.name).c_str());
        logProjection = true;
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

    // Atajo "Part": anade el objeto a la escena y al Explorer (insertar
    // lo selecciona, lo que refresca Properties via onSelectionChanged).
    workspace.setOnAddPart([&]() {
        const std::string name = scene.addPart().name;
        place.addObject(name);
        saveScene();
        logProjection = true;
        SK_INFO("Part anadido: %s", name.c_str());
    });

    // Cambio de herramienta (botones de la banda o teclas 1-4).
    workspace.setOnToolChanged([](int tool) {
        static const char* names[] = {"seleccionar", "mover", "escalar", "rotar"};
        SK_INFO("herramienta: %s", names[tool]);
    });

    // Seleccion en el Explorer -> Properties (objetos de la escena o la
    // raiz dimension01). El estado de seleccion (multi) vive aqui y el
    // arbol lo refleja.
    place.setOnSelectionChanged([&](const std::string& name) {
        const sk::SceneObject* object =
            name.empty() ? nullptr : scene.findByName(name);
        if (object) {
            selection = {name};
            primary = name;
            place.showObject(*object);
        } else {
            selection.clear();
            primary.clear();
            place.showNoSelection();
            if (!name.empty()) place.showRoot(); // raiz dimension01
        }
    });

    // ModelScript: boton "Cambiar..." de PLACE -> selector de scripts;
    // la escena guarda el objeto.script y se escribe en disco.
    place.setOnAssocEdit([&](const std::string& objectName) {
        sk::SceneObject* object = scene.findByName(objectName);
        if (!object) return;
        std::string rel;
        const HWND owner = static_cast<HWND>(window.nativeHandle());
        if (!sk::ui::pickScript(owner, active, object->script, rel)) return;
        object->script = rel;
        saveScene();
        place.showObject(*object);   // refresca el Asociado a en PLACE
        code.refreshProperties();
        SK_INFO("Asociacion de %s: %s", objectName.c_str(),
                rel.empty() ? "(ninguno)" : rel.c_str());
    });

    // CODE: el panel Properties consulta el objeto asociado a un script.
    code.setOnQueryAssoc([&](const std::string& rel) -> std::string {
        for (const sk::SceneObject& object : scene.objects()) {
            if (object.script == rel) return object.name;
        }
        return "";
    });

    // Borrar un script (o una carpeta con sus hijos) limpia las
    // asociaciones de la escena que apuntaban a esa rel.
    code.setOnScriptRemoved([&](const std::string& rel) {
        bool changed = false;
        for (sk::SceneObject& object : scene.objects()) {
            if (object.script.empty()) continue;
            if (object.script == rel ||
                object.script.rfind(rel + "/", 0) == 0) {
                object.script.clear();
                changed = true;
            }
        }
        if (changed) saveScene();
    });

    // Renombrar (archivo o carpeta) reescribe las rels afectadas.
    code.setOnScriptRenamed([&](const std::string& oldRel,
                                 const std::string& newRel) {
        bool changed = false;
        for (sk::SceneObject& object : scene.objects()) {
            if (object.script == oldRel) {
                object.script = newRel;
                changed = true;
            } else if (object.script.rfind(oldRel + "/", 0) == 0) {
                object.script = newRel + object.script.substr(oldRel.size());
                changed = true;
            }
        }
        if (changed) saveScene();
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
    bool camKeysWasDown = false;
    int toolKeyWasDown = 0;

    // Arrastre de seleccion (marquee) en el viewport de PLACE.
    bool dragPending = false;  // pressed dentro del viewport, sin mover
    bool dragActive = false;   // supero el umbral: es un arrastre
    sk::Vec2 dragStart{};
    auto inViewport = [&](const sk::Vec2& p) {
        if (!view3d || !place.visible()) return false;
        const float x0 = static_cast<float>(sk::PlaceView::kPropertiesWidth);
        const float x1 = static_cast<float>(window.framebufferWidth()) -
                         static_cast<float>(sk::PlaceView::kExplorerWidth);
        const float y0 = static_cast<float>(sk::WorkspaceTabs::kTopBandHeight);
        return p.x >= x0 && p.x <= x1 && p.y >= y0 &&
               p.y <= static_cast<float>(window.framebufferHeight());
    };
    // Pinta Properties + caret del Explorer tras cambiar la seleccion.
    auto applySelection = [&](std::vector<std::string> names,
                              const std::string& newPrimary) {
        selection = std::move(names);
        primary = newPrimary;
        place.syncTreeSelection(primary);
        if (selection.empty()) {
            place.showNoSelection();
        } else if (selection.size() == 1) {
            const sk::SceneObject* object = scene.findByName(selection.front());
            if (object) {
                place.showObject(*object);
            } else {
                place.showNoSelection();
            }
        } else {
            place.showMultiple(static_cast<int>(selection.size()));
        }
    };

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

        // Esc: cancela el arrastre de seleccion si esta en curso; si no,
        // vuelve al panel de proyectos. Solo con la ventana enfocada
        // (keyDown usa GetAsyncKeyState, que es global).
        const bool escDown = window.keyDown(VK_ESCAPE) && window.hasFocus();
        if (escDown && !escWasDown && (dragPending || dragActive)) {
            dragPending = false;
            dragActive = false;
            SK_INFO("marquee cancelado");
        } else if (escDown && !escWasDown) {
            view3d = false;
            dragPending = false;
            dragActive = false;
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

        // Log al empezar a mover la camara (mismo criterio de foco que
        // Camera::update): las pruebas comprueban que WASD/Q/E no
        // funcionan con otra aplicacion en primer plano.
        const bool camKeys = window.hasFocus() && !window.textInputFocused() &&
                             (window.keyDown('W') || window.keyDown('A') ||
                              window.keyDown('S') || window.keyDown('D') ||
                              window.keyDown('Q') || window.keyDown('E'));
        if (camKeys && !camKeysWasDown) {
            const sk::Vec3 p = camera.position();
            SK_INFO("camara (%.2f,%.2f,%.2f)", p.x, p.y, p.z);
        }
        camKeysWasDown = camKeys;

        // Teclas 1-4: cambiar de herramienta (solo con la ventana
        // enfocada, en PLACE y sin escribir en un campo de texto).
        const bool toolKeysOk = window.hasFocus() && !window.textInputFocused() &&
                                workspace.visible() && place.visible();
        int toolKeyNow = 0;
        if (toolKeysOk) {
            if (window.keyDown('1')) toolKeyNow = 1;
            else if (window.keyDown('2')) toolKeyNow = 2;
            else if (window.keyDown('3')) toolKeyNow = 3;
            else if (window.keyDown('4')) toolKeyNow = 4;
        }
        if (toolKeyNow != 0 && toolKeyNow != toolKeyWasDown) {
            workspace.setTool(toolKeyNow - 1);
        }
        toolKeyWasDown = toolKeyNow;

        const float aspect = (window.framebufferHeight() > 0)
            ? static_cast<float>(window.framebufferWidth()) /
              static_cast<float>(window.framebufferHeight())
            : 16.0f / 9.0f;
        const sk::Mat4 projection = sk::perspective(sk::radians(60.0f), aspect, 0.1f, 200.0f);
        const sk::Mat4 viewProj = projection * camera.view();

        // --- Seleccion en el viewport (click / Ctrl+click / marquee) ---
        const int vpW = window.framebufferWidth();
        const int vpH = window.framebufferHeight();
        const std::optional<sk::Vec2> pressed = window.consumeLeftPressed();
        const std::optional<sk::Vec2> released = window.consumeLeftReleased();
        if (pressed && inViewport(*pressed)) {
            dragPending = true;
            dragActive = false;
            dragStart = *pressed;
        }
        if (dragPending && !dragActive) {
            const int thX = GetSystemMetrics(SM_CXDRAG);
            const int thY = GetSystemMetrics(SM_CYDRAG);
            const sk::Vec2 pos = window.mousePos();
            if (std::abs(pos.x - dragStart.x) > thX ||
                std::abs(pos.y - dragStart.y) > thY) {
                dragActive = true;
                SK_INFO("marquee (%d,%d)-(%d,%d)",
                        static_cast<int>(dragStart.x),
                        static_cast<int>(dragStart.y),
                        static_cast<int>(pos.x), static_cast<int>(pos.y));
            }
        }
        if (released && (dragPending || dragActive)) {
            const bool wasDrag = dragActive;
            dragPending = false;
            dragActive = false;
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            if (wasDrag) {
                // Rectangulo de arrastre, clamado al viewport.
                float x0 = std::fmin(dragStart.x, released->x);
                float y0 = std::fmin(dragStart.y, released->y);
                float x1 = std::fmax(dragStart.x, released->x);
                float y1 = std::fmax(dragStart.y, released->y);
                const float vx0 = static_cast<float>(sk::PlaceView::kPropertiesWidth);
                const float vx1 = static_cast<float>(vpW) -
                                  static_cast<float>(sk::PlaceView::kExplorerWidth);
                const float vy0 = static_cast<float>(sk::WorkspaceTabs::kTopBandHeight);
                const float vy1 = static_cast<float>(vpH);
                x0 = std::fmax(x0, vx0);
                y0 = std::fmax(y0, vy0);
                x1 = std::fmin(x1, vx1);
                y1 = std::fmin(y1, vy1);
                std::vector<std::string> hits;
                if (x0 < x1 && y0 < y1) {
                    hits = sk::selectInRect(scene, viewProj,
                                            sk::Vec2{x0, y0}, sk::Vec2{x1, y1},
                                            vpW, vpH);
                }
                SK_INFO("marquee -> %d objetos", static_cast<int>(hits.size()));
                std::string newPrimary = primary;
                if (std::find(hits.begin(), hits.end(), primary) == hits.end()) {
                    newPrimary = hits.empty() ? std::string() : hits.front();
                }
                applySelection(std::move(hits), newPrimary);
            } else {
                // Click simple: el objeto mas cercano bajo el puntero.
                const std::string hit = sk::pickObject(
                    scene, sk::rayFromCamera(camera, aspect, 60.0f, *released,
                                             vpW, vpH));
                SK_INFO("pick (%d,%d) -> %s", static_cast<int>(released->x),
                        static_cast<int>(released->y),
                        hit.empty() ? "(nada)" : hit.c_str());
                std::vector<std::string> names = selection;
                std::string newPrimary = primary;
                if (hit.empty()) {
                    names.clear();       // click en vacio: deselecciona
                    newPrimary.clear();
                } else if (ctrl) {
                    const auto it = std::find(names.begin(), names.end(), hit);
                    if (it != names.end()) {
                        names.erase(it); // quitar de la seleccion
                        if (newPrimary == hit) {
                            newPrimary =
                                names.empty() ? std::string() : names.front();
                        }
                    } else {
                        names.push_back(hit); // anadir (Ctrl)
                        newPrimary = hit;
                    }
                } else {
                    names = {hit};
                    newPrimary = hit;
                }
                applySelection(std::move(names), newPrimary);
            }
        }

        if (logProjection && place.visible()) {
            logProjection = false;
            for (const sk::SceneObject& object : scene.objects()) {
                sk::Vec3 ndc;
                if (sk::transformPoint(viewProj, object.position, ndc)) {
                    const float px = (ndc.x * 0.5f + 0.5f) * vpW;
                    const float py = (ndc.y * 0.5f + 0.5f) * vpH;
                    SK_INFO("%s pantalla (%d,%d)", object.name.c_str(),
                            static_cast<int>(px), static_cast<int>(py));
                }
            }
        }

        std::vector<sk::Mat4> models;
        models.reserve(scene.objects().size());
        for (const sk::SceneObject& object : scene.objects()) {
            models.push_back(object.modelMatrix());
        }

        // Contorno celeste: el cubo alambre un 2% mas grande que el
        // objeto, para que asoma por el borde de la malla.
        std::vector<sk::Mat4> outlines;
        outlines.reserve(selection.size());
        for (const std::string& name : selection) {
            const sk::SceneObject* object = scene.findByName(name);
            if (object) {
                outlines.push_back(object->modelMatrix() *
                                   sk::scale(sk::Vec3{1.02f, 1.02f, 1.02f}));
            }
        }

        // Rectangulo de arrastre mientras dura el marquee.
        sk::ScreenRect marquee;
        if (dragActive && inViewport(dragStart)) {
            marquee.valid = true;
            marquee.x0 = std::fmin(dragStart.x, window.mousePos().x);
            marquee.y0 = std::fmin(dragStart.y, window.mousePos().y);
            marquee.x1 = std::fmax(dragStart.x, window.mousePos().x);
            marquee.y1 = std::fmax(dragStart.y, window.mousePos().y);
        }

        // Lineas del gizmo: de momento vacias (se rellenan en el paso de
        // herramientas).
        const std::vector<sk::LineVertex> gizmoLines;

        if (!renderer.drawFrame(viewProj, models, outlines, marquee, gizmoLines)) {
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
