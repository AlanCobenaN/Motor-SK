#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "platform/window.h"

namespace {

int parseFramesArg(int argc, char** argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0) {
            return std::atoi(argv[i + 1]);
        }
    }
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    const int maxFrames = parseFramesArg(argc, argv);

    sk::Window window;
    if (!window.create(1280, 720, "MotorSK")) {
        return 1;
    }

    int frame = 0;
    while (window.pumpEvents() && (maxFrames < 0 || frame < maxFrames)) {
        if (window.consumeResized()) {
            SK_INFO("resize: %dx%d", window.framebufferWidth(), window.framebufferHeight());
        }

        const float wheel = window.consumeWheel();
        if (wheel != 0.0f) {
            SK_INFO("wheel: %+.0f", wheel);
        }

        if (frame % 60 == 0) {
            const bool w = window.keyDown('W');
            const bool rmb = window.mouseRightDown();
            SK_INFO("frame %d  W=%d RMB=%d", frame, w ? 1 : 0, rmb ? 1 : 0);
        }

        ++frame;
    }

    SK_INFO("saliendo tras %d frames", frame);
    return 0;
}
