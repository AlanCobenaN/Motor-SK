#include <windows.h>

#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "math/math.h"
#include "platform/window.h"
#include "render/renderer.h"
#include "scene/camera.h"

namespace {

int parseFramesArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) {
            return std::atoi(argv[i + 1]);
        }
    }
    return -1;
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
    const int maxFrames = parseFramesArg(argc, argv);

    // Medal y OBS registran capas implicitas de Vulkan cuyos hooks
    // interceptan la creacion de swapchain y corrompen el heap de este
    // proceso (confirmado con AddressSanitizer). Cada capa declara una
    // variable de deshabilitacion que se lee al crear la instancia.
    _putenv_s("DISABLE_VULKAN_MEDAL_OBS_CAPTURE", "1");
    _putenv_s("DISABLE_VULKAN_OBS_CAPTURE", "1");
    SK_INFO("Capas de captura de Medal/OBS desactivadas");

    sk::Window window;
    if (!window.create(1280, 720, "MotorSK")) {
        return 1;
    }

    sk::Renderer renderer;
    if (!renderer.init(window)) {
        SK_ERROR("No se pudo inicializar el renderer");
        return 1;
    }

    sk::Camera camera;
    const double startTime = nowSeconds();
    double lastTime = startTime;
    int frame = 0;

    while (window.pumpEvents() && (maxFrames < 0 || frame < maxFrames)) {
        const double now = nowSeconds();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f; // proteccion contra picos (ventana arrastrada)

        if (window.consumeResized()) {
            SK_INFO("resize: %dx%d", window.framebufferWidth(), window.framebufferHeight());
        }

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

    renderer.shutdown();
    window.destroy();

    const double elapsed = nowSeconds() - startTime;
    SK_INFO("saliendo tras %d frames (%.1fs)", frame, elapsed);
    return 0;
}
