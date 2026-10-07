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
#include "scene/gizmo.h"
#include "scene/picking.h"
#include "scene/scene.h"
#include "scene/snapping.h"
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

// Nombres de las herramientas de la banda (indices 0-3), para los
// registros de cambio y de arrastre del gizmo.
const char* kToolNames[] = {"seleccionar", "mover", "escalar", "rotar"};

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
    workspace.setOnAddPart([&](int shape) {
        const sk::Shape s = (shape >= 0 && shape < static_cast<int>(sk::Shape::Count))
                                ? static_cast<sk::Shape>(shape)
                                : sk::Shape::Cube;
        const std::string name = scene.addPart(s).name;
        place.addObject(name);
        saveScene();
        logProjection = true;
        SK_INFO("Part anadido: %s (%s)", name.c_str(), sk::shapeName(s));
    });

    // Cambio de herramienta (botones de la banda o teclas 1-4).
    workspace.setOnToolChanged([](int tool) {
        SK_INFO("herramienta: %s", kToolNames[tool]);
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

    // Properties Transform editable: al perder el foco un campo se
    // aplica al objeto, se guarda la escena y se reescriben los campos
    // con el formato normalizado.
    place.setOnTransformEdited([&](const std::string& name, int id,
                                   float value) {
        sk::SceneObject* object = scene.findByName(name);
        if (!object) return;
        const int row = (id - 200) / 3;
        const int axis = (id - 200) % 3;
        static const char* kRows[] = {"posicion", "rotacion", "escala"};
        static const char* kAxes[] = {"x", "y", "z"};
        sk::Vec3* target = &object->position;
        if (row == 1) target = &object->rotation;
        else if (row == 2) target = &object->scale;
        (&target->x)[axis] = value;
        saveScene();
        place.showObject(*object);
        SK_INFO("Properties %s: %s.%s = %.3g", name.c_str(), kRows[row],
                kAxes[axis], value);
    });

    // Properties Nombre editable: al perder el foco o con Enter se
    // renombra el objeto en la escena, en el Explorer y en los paneles.
    // Nombre duplicado/vacio lo rechaza la escena y el panel restaura
    // el texto anterior.
    place.setOnNameEdited([&](const std::string& oldName,
                              const std::string& newName) {
        if (!scene.renameObject(oldName, newName)) return false;
        if (primary == oldName) primary = newName;
        for (std::string& name : selection) {
            if (name == oldName) name = newName;
        }
        place.renameTreeItem(oldName, newName);
        saveScene();
        SK_INFO("Part renombrado: %s -> %s", oldName.c_str(),
                newName.c_str());
        if (const sk::SceneObject* object = scene.findByName(newName)) {
            place.showObject(*object); // campo y selectedName_ al dia
        }
        code.refreshProperties(); // "Asociado a" en CODE
        return true;
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

    // Gizmo de herramientas: arrastre activo + eje bajo el puntero.
    sk::GizmoDrag gizmoDrag;

    // Arrastre de objeto: mantener pulsado el cuerpo de un parte (sin tocar
    // un asa del gizmo) y arrastrarlo con el puntero. La caja del grupo
    // se apoya en la cara del otro parte que esta bajo el puntero (o en
    // el suelo Y=0 si no hay nada): ver scene/snapping.h.
    struct ObjDragStart {
        std::string name;
        sk::Vec3 pos;
        sk::Vec3 rot;
    };
    bool objDragActive = false;
    std::vector<ObjDragStart> objDragStart;
    // Punto de agarre menos centro del lead al empezar (mundo): fija la
    // posicion del grupo respecto al puntero durante el arrastre.
    sk::Vec3 grabOffset{};
    std::string pressHit; // objeto bajo el punto de presion (o vacio)
    bool pressCtrl = false;
    // Cara bajo el puntero durante el arrastre: se registra una sola vez
    // por destino para que las pruebas vean donde se apoya el parte sin
    // repetir el log en cada frame.
    bool snapLogged = false;
    std::string snapSurface;
    sk::Vec3 snapNormal{};
    int hoverAxis = -1;
    // Registro "gizmo eje X punta (x,y)": una sola vez por punta, para
    // que las pruebas sepan donde pulsar sin repetir el log en cada frame.
    int lastHoverTool = -1;
    int lastHoverAxis = -1;
    int lastHoverTx = -1;
    int lastHoverTy = -1;
    std::string lastHoverPrimary;
    bool cursorForced = false;
    // Puntas de los ejes logeadas al cambiar de herramienta o de
    // seleccion (y tras cada arrastre): las pruebas leen de aqui donde
    // poner el puntero sin tener que barrer el viewport.
    int lastTipsTool = -1;
    std::string lastTipsPrimary;
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

        // Esc: cancela el arrastre del gizmo si esta en curso; si no, el
        // del marquee; si no, vuelve al panel de proyectos. Solo con la
        // ventana enfocada (keyDown usa GetAsyncKeyState, que es global).
        const bool escDown = window.keyDown(VK_ESCAPE) && window.hasFocus();
        if (escDown && !escWasDown && gizmoDrag.active) {
            sk::gizmoRevert(gizmoDrag, scene);
            gizmoDrag.active = false;
            lastTipsTool = -1; // las puntas vuelven a su sitio
            SK_INFO("gizmo cancelado");
            applySelection(selection, primary);
        } else if (escDown && !escWasDown && objDragActive) {
            // Revierte el arrastre del objeto a sus poses de inicio
            // (posicion y rotacion: el snap de superficie puede haber
            // girado el grupo).
            for (const ObjDragStart& start : objDragStart) {
                if (sk::SceneObject* object = scene.findByName(start.name)) {
                    object->position = start.pos;
                    object->rotation = start.rot;
                }
            }
            objDragActive = false;
            objDragStart.clear();
            dragPending = false;
            SK_INFO("drag cancelado");
            applySelection(selection, primary);
        } else if (escDown && !escWasDown && (dragPending || dragActive)) {
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
        // enfocada, en PLACE, sin escribir en un campo de texto y sin
        // un arrastre del gizmo en curso).
        const bool toolKeysOk = window.hasFocus() && !window.textInputFocused() &&
                                workspace.visible() && place.visible() &&
                                !gizmoDrag.active;
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

        // --- Gizmo de herramientas: estado, hover y arrastre ---
        const int tool = workspace.tool();
        const sk::SceneObject* primaryObj =
            primary.empty() ? nullptr : scene.findByName(primary);
        const bool gizmoReady =
            place.visible() && workspace.visible() && tool >= 1 && primaryObj;
        const sk::Vec3 gizmoOrigin = primaryObj ? primaryObj->position : sk::Vec3{};
        float gizmoScaleMax = 1.0f;
        if (primaryObj) {
            gizmoScaleMax = std::fmax(
                std::fabs(primaryObj->scale.x),
                std::fmax(std::fabs(primaryObj->scale.y),
                          std::fabs(primaryObj->scale.z)));
        }
        // Durante el arrastre el tamano visual se congela (L del arrastre).
        if (gizmoDrag.active) gizmoScaleMax = 2.0f * gizmoDrag.L - 2.0f;
        const sk::Vec3 gizmoViewDir =
            sk::normalize(camera.position() - gizmoOrigin);

        // Eje bajo el puntero (solo con el raton en el viewport y sin
        // arrastre en curso). El hit test ignora los colores, asi que las
        // lineas de sondeo se construyen sin resaltar.
        if (gizmoReady && !gizmoDrag.active && !objDragActive &&
            inViewport(window.mousePos())) {
            const std::vector<sk::GizmoLine> probe =
                sk::buildGizmo(tool, gizmoOrigin, gizmoScaleMax, gizmoViewDir, -1);
            hoverAxis = sk::hitTestGizmo(probe, window.mousePos(), viewProj, vpW, vpH);
        } else {
            hoverAxis = -1;
        }

        // Puntas de los ejes al cambiar de herramienta o de objeto
        // primario (o tras un arrastre, que deja la marca sin marcar).
        if (gizmoReady && !gizmoDrag.active && !objDragActive &&
            (tool != lastTipsTool || primary != lastTipsPrimary)) {
            sk::Vec3 ndc;
            int px[3] = {-1, -1, -1};
            int py[3] = {-1, -1, -1};
            for (int axis = 0; axis < 3; ++axis) {
                const sk::Vec3 tip =
                    sk::gizmoHandlePoint(tool, axis, gizmoOrigin, gizmoScaleMax);
                if (sk::transformPoint(viewProj, tip, ndc)) {
                    px[axis] = static_cast<int>((ndc.x * 0.5f + 0.5f) * vpW);
                    py[axis] = static_cast<int>((ndc.y * 0.5f + 0.5f) * vpH);
                }
            }
            SK_INFO("gizmo puntas X(%d,%d) Y(%d,%d) Z(%d,%d)", px[0], py[0],
                    px[1], py[1], px[2], py[2]);
            if (tool == 1 || tool == 2) {
                // Mangos negativos (3..5), en un registro aparte para no
                // mezclarlos con el recuento de "gizmo puntas".
                int nx[3] = {-1, -1, -1};
                int ny[3] = {-1, -1, -1};
                for (int axis = 0; axis < 3; ++axis) {
                    const sk::Vec3 tip = sk::gizmoHandlePoint(
                        tool, axis + 3, gizmoOrigin, gizmoScaleMax);
                    if (sk::transformPoint(viewProj, tip, ndc)) {
                        nx[axis] = static_cast<int>((ndc.x * 0.5f + 0.5f) * vpW);
                        ny[axis] = static_cast<int>((ndc.y * 0.5f + 0.5f) * vpH);
                    }
                }
                SK_INFO("puntas neg X(%d,%d) Y(%d,%d) Z(%d,%d)", nx[0], ny[0],
                        nx[1], ny[1], nx[2], ny[2]);
            }
            lastTipsTool = tool;
            lastTipsPrimary = primary;
            lastHoverAxis = -1; // que el hover vuelva a registrar
        }

        if (pressed && inViewport(*pressed)) {
            bool startedGizmo = false;
            // El eje se recalcula con el punto exacto del press: el hover
            // (hoverAxis) se deriva de mousePos y puede haberse perdido si
            // un movimiento real del raton llego entre el hover y el click.
            int pressAxis = -1;
            if (gizmoReady && !gizmoDrag.active) {
                const std::vector<sk::GizmoLine> probe = sk::buildGizmo(
                    tool, gizmoOrigin, gizmoScaleMax, gizmoViewDir, -1);
                pressAxis = sk::hitTestGizmo(probe, *pressed, viewProj, vpW, vpH);
            }
            if (pressAxis >= 0) {
                const sk::Ray ray =
                    sk::rayFromCamera(camera, aspect, 60.0f, *pressed, vpW, vpH);
                startedGizmo = sk::gizmoBegin(gizmoDrag, tool, pressAxis, scene,
                                              selection, gizmoOrigin, gizmoScaleMax,
                                              gizmoViewDir, ray);
                if (startedGizmo) {
                    // El arrastre del gizmo sustituye al marquee.
                    dragPending = false;
                    dragActive = false;
                    if (pressAxis < 3) {
                        SK_INFO("gizmo %s: arrastre eje %c", kToolNames[tool],
                                "XYZ"[pressAxis]);
                    } else {
                        SK_INFO("gizmo %s: arrastre eje -%c", kToolNames[tool],
                                "XYZ"[pressAxis - 3]);
                    }
                }
            }
            if (!startedGizmo && !gizmoDrag.active) {
                dragPending = true;
                dragActive = false;
                dragStart = *pressed;
                // Cuerpo de un parte bajo el puntero: candidato a arrastre
                // de objeto (se confirma al superar el umbral de arrastre).
                pressCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                pressHit.clear();
                if (!pressCtrl) {
                    pressHit = sk::pickObject(
                        scene, sk::rayFromCamera(camera, aspect, 60.0f,
                                                 *pressed, vpW, vpH));
                }
            }
        }

        // Arrastre del objeto: la caja del grupo arrastrado se apoya en
        // la cara del parte que esta bajo el puntero (cualquier cara:
        // suelo, techo, pared o inclinada), con la rotacion del lead
        // alineada a esa cara y la posicion en su plano a la rejilla;
        // sin nada debajo el rayo cae al plano del suelo Y=0. Alt
        // conserva la orientacion y Shift desactiva la rejilla.
        if (objDragActive) {
            const sk::Ray ray0 = sk::rayFromCamera(
                camera, aspect, 60.0f, dragStart, vpW, vpH);
            const sk::Ray ray1 = sk::rayFromCamera(
                camera, aspect, 60.0f, window.mousePos(), vpW, vpH);
            std::vector<std::string> dragged;
            std::vector<sk::DragPose> startPose;
            dragged.reserve(objDragStart.size());
            startPose.reserve(objDragStart.size());
            int leadIndex = -1;
            for (const ObjDragStart& start : objDragStart) {
                if (start.name == pressHit) {
                    leadIndex = static_cast<int>(dragged.size());
                }
                dragged.push_back(start.name);
                startPose.push_back({start.name, start.pos, start.rot});
            }
            const bool alignSurface = (GetKeyState(VK_MENU) & 0x8000) == 0;
            const bool snapGrid = (GetKeyState(VK_SHIFT) & 0x8000) == 0;
            sk::DragResult result;
            sk::computeSurfaceDrag(scene, ray0, ray1, dragged, leadIndex,
                                   grabOffset, startPose, alignSurface,
                                   snapGrid ? sk::kDragGrid : 0.0f, result);
            for (const sk::DragPose& pose : result.poses) {
                if (sk::SceneObject* object = scene.findByName(pose.name)) {
                    object->position = pose.position;
                    object->rotation = pose.rotation;
                }
            }
            // Registro de la cara de destino: una sola vez por cara (las
            // pruebas leen donde se apoya el parte sin spam de logs).
            if (!snapLogged || result.surfaceName != snapSurface ||
                sk::length(result.surfaceNormal - snapNormal) > 1e-3f) {
                if (result.onSurface) {
                    SK_INFO("snap %s: cara de %s normal (%.2f,%.2f,%.2f)",
                            pressHit.c_str(), result.surfaceName.c_str(),
                            result.surfaceNormal.x, result.surfaceNormal.y,
                            result.surfaceNormal.z);
                } else if (result.groundHit) {
                    SK_INFO("snap %s: suelo Y=0", pressHit.c_str());
                } else {
                    SK_INFO("snap %s: cielo (x/z del plano de camara, "
                                          "apoyo en el suelo)",
                            pressHit.c_str());
                }
                snapLogged = true;
                snapSurface = result.surfaceName;
                snapNormal = result.surfaceNormal;
            }
            if (selection.size() == 1) {
                if (const sk::SceneObject* object =
                        scene.findByName(selection.front())) {
                    place.showObject(*object); // Transform al dia
                }
            }
        }

        // Arrastre del gizmo: el delta se aplica a toda la seleccion.
        if (gizmoDrag.active) {
            const sk::Ray ray = sk::rayFromCamera(
                camera, aspect, 60.0f, window.mousePos(), vpW, vpH);
            if (sk::gizmoUpdate(gizmoDrag, ray, scene) && selection.size() == 1 &&
                primaryObj) {
                place.showObject(*primaryObj); // Transform al dia
            }
        }

        // Registro al apuntar a un eje (una sola vez por punta: las
        // pruebas hacen hover, leen la punta y arrastran).
        if (hoverAxis >= 0 && gizmoReady && !gizmoDrag.active) {
            const sk::Vec3 tip = sk::gizmoHandlePoint(
                tool, hoverAxis, gizmoOrigin, gizmoScaleMax);
            int tx = -1;
            int ty = -1;
            sk::Vec3 ndc;
            if (sk::transformPoint(viewProj, tip, ndc)) {
                tx = static_cast<int>((ndc.x * 0.5f + 0.5f) * vpW);
                ty = static_cast<int>((ndc.y * 0.5f + 0.5f) * vpH);
            }
            if (hoverAxis != lastHoverAxis || tool != lastHoverTool ||
                primary != lastHoverPrimary || tx != lastHoverTx ||
                ty != lastHoverTy) {
                if (hoverAxis < 3) {
                    SK_INFO("gizmo eje %c punta (%d,%d)", "XYZ"[hoverAxis], tx,
                            ty);
                } else {
                    SK_INFO("gizmo eje -%c punta (%d,%d)", "XYZ"[hoverAxis - 3],
                            tx, ty);
                }
                lastHoverTool = tool;
                lastHoverAxis = hoverAxis;
                lastHoverPrimary = primary;
                lastHoverTx = tx;
                lastHoverTy = ty;
            }
        } else if (hoverAxis < 0) {
            lastHoverAxis = -1;
        }

        // Cursor de cruz de mover sobre el gizmo o mientras se arrastra.
        const bool wantMoveCursor =
            gizmoDrag.active || objDragActive || hoverAxis >= 0;
        if (wantMoveCursor) {
            SetCursor(LoadCursorA(nullptr, IDC_SIZEALL));
            cursorForced = true;
        } else if (cursorForced) {
            SetCursor(LoadCursorA(nullptr, IDC_ARROW));
            cursorForced = false;
        }

        if (dragPending && !dragActive && !objDragActive) {
            const int thX = GetSystemMetrics(SM_CXDRAG);
            const int thY = GetSystemMetrics(SM_CYDRAG);
            const sk::Vec2 pos = window.mousePos();
            if (std::abs(pos.x - dragStart.x) > thX ||
                std::abs(pos.y - dragStart.y) > thY) {
                if (!pressHit.empty()) {
                    // Es el cuerpo de un parte: arrastre de objeto en vez
                    // de marquee (si no estaba seleccionado, pasa a serlo).
                    if (std::find(selection.begin(), selection.end(),
                                  pressHit) == selection.end()) {
                        applySelection({pressHit}, pressHit);
                    }
                    objDragActive = true;
                    objDragStart.clear();
                    for (const std::string& name : selection) {
                        if (const sk::SceneObject* object =
                                scene.findByName(name)) {
                            objDragStart.push_back(
                                {name, object->position, object->rotation});
                        }
                    }
                    // Punto de agarre (donde se pulso sobre el cuerpo)
                    // menos el centro del lead: fija la posicion del
                    // grupo respecto al puntero durante todo el arrastre.
                    const sk::Ray ray0 = sk::rayFromCamera(
                        camera, aspect, 60.0f, dragStart, vpW, vpH);
                    const sk::SceneObject* hitObj = scene.findByName(pressHit);
                    grabOffset = {};
                    if (hitObj) {
                        sk::Vec3 grabPoint{};
                        sk::Vec3 grabNormal{};
                        if (sk::rayObjectFace(*hitObj, ray0, grabPoint,
                                              grabNormal)) {
                            grabOffset = grabPoint - hitObj->position;
                        }
                    }
                    snapLogged = false; // el primer frame registra la cara
                    snapSurface.clear();
                    snapNormal = {};
                    dragPending = false;
                    SK_INFO("drag %s: arrastre", pressHit.c_str());
                } else {
                    dragActive = true;
                    SK_INFO("marquee (%d,%d)-(%d,%d)",
                            static_cast<int>(dragStart.x),
                            static_cast<int>(dragStart.y),
                            static_cast<int>(pos.x), static_cast<int>(pos.y));
                }
            }
        }
        if (released && objDragActive) {
            // Fin del arrastre: registrar el cambio y guardar.
            for (const ObjDragStart& start : objDragStart) {
                const sk::SceneObject* object = scene.findByName(start.name);
                if (!object) continue;
                SK_INFO("drag %s: pos (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) "
                        "rot (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f)",
                        start.name.c_str(), start.pos.x, start.pos.y,
                        start.pos.z, object->position.x, object->position.y,
                        object->position.z, start.rot.x, start.rot.y,
                        start.rot.z, object->rotation.x, object->rotation.y,
                        object->rotation.z);
            }
            objDragActive = false;
            objDragStart.clear();
            pressHit.clear();
            dragPending = false;
            lastTipsTool = -1; // las puntas del gizmo siguen al objeto
            saveScene();
            applySelection(selection, primary);
        } else if (released && gizmoDrag.active) {
            // Fin del arrastre: registrar el cambio, guardar y refrescar.
            for (const sk::GizmoSnapshot& snap : gizmoDrag.snapshots) {
                const sk::SceneObject* object = scene.findByName(snap.name);
                if (!object) continue;
                SK_INFO("gizmo %s: %s pos (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) "
                        "rot (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f) "
                        "esc (%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f)",
                        kToolNames[gizmoDrag.tool], snap.name.c_str(),
                        snap.position.x, snap.position.y, snap.position.z,
                        object->position.x, object->position.y,
                        object->position.z, snap.rotation.x, snap.rotation.y,
                        snap.rotation.z, object->rotation.x, object->rotation.y,
                        object->rotation.z, snap.scale.x, snap.scale.y,
                        snap.scale.z, object->scale.x, object->scale.y,
                        object->scale.z);
            }
            gizmoDrag.active = false;
            lastTipsTool = -1; // las puntas han podido cambiar
            saveScene();
            applySelection(selection, primary);
        } else if (released && (dragPending || dragActive)) {
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
        std::vector<int> shapeIds;
        models.reserve(scene.objects().size());
        shapeIds.reserve(scene.objects().size());
        for (const sk::SceneObject& object : scene.objects()) {
            models.push_back(object.modelMatrix());
            shapeIds.push_back(static_cast<int>(object.shape));
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

        // Rectangulo de arrastre mientras dura el marquee. Se muestra
        // igual que la seleccion al soltar: clamado al viewport para que
        // el rectangulo visible coincida con lo que se selecciona.
        sk::ScreenRect marquee;
        if (dragActive && inViewport(dragStart)) {
            const sk::Vec2 pos = window.mousePos();
            float x0 = std::fmin(dragStart.x, pos.x);
            float y0 = std::fmin(dragStart.y, pos.y);
            float x1 = std::fmax(dragStart.x, pos.x);
            float y1 = std::fmax(dragStart.y, pos.y);
            x0 = std::fmax(x0, static_cast<float>(
                                   sk::PlaceView::kPropertiesWidth));
            y0 = std::fmax(y0, static_cast<float>(
                                   sk::WorkspaceTabs::kTopBandHeight));
            x1 = std::fmin(x1, static_cast<float>(
                                   vpW - sk::PlaceView::kExplorerWidth));
            y1 = std::fmin(y1, static_cast<float>(vpH));
            marquee.valid = true;
            marquee.x0 = x0;
            marquee.y0 = y0;
            marquee.x1 = x1;
            marquee.y1 = y1;
        }

        // Lineas del gizmo: un vertice por extremo de segmento (el
        // renderer clama al buffer si hubiera mas de kGizmoCapacity).
        std::vector<sk::LineVertex> gizmoLines;
        if (gizmoReady) {
            // Resalta el mango bajo el puntero o el mango agarrado.
            int paintAxis = hoverAxis;
            if (gizmoDrag.active) {
                paintAxis = gizmoDrag.axis +
                            (gizmoDrag.axisSign < 0 ? 3 : 0);
            }
            const std::vector<sk::GizmoLine> geo = sk::buildGizmo(
                gizmoDrag.active ? gizmoDrag.tool : tool, gizmoOrigin,
                gizmoScaleMax, gizmoViewDir, paintAxis);
            gizmoLines.reserve(geo.size() * 2);
            for (const sk::GizmoLine& line : geo) {
                gizmoLines.push_back({{line.a.x, line.a.y, line.a.z},
                                      {line.color.x, line.color.y, line.color.z}});
                gizmoLines.push_back({{line.b.x, line.b.y, line.b.z},
                                      {line.color.x, line.color.y, line.color.z}});
            }
        }

        if (!renderer.drawFrame(viewProj, models, shapeIds, outlines, marquee, gizmoLines)) {
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
