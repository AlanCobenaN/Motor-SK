# Motor SK

Motor de juegos 3D escrito en C++20 con Vulkan, enfocado en Windows hoy y con
rutas de portabilidad a Linux y Android.

## Estado actual

Fase: **1 — ventana y cámara** (en curso). El `.exe` abre una ventana con
swapchain de Vulkan, rejilla de referencia y cámara libre
(WASD + click derecho para mirar + scroll para zoom). Falta el panel de
proyectos.

| Componente | Versión | Notas |
|---|---|---|
| Compilador | MSVC 19.44 (VS 2022 Build Tools) | `vcvarsall.bat x64` |
| CMake | 4.4.4 | `C:\Users\alanj\AppData\Local\Programs\CMake\bin` |
| Vulkan SDK | 1.4.363.0 | LunarG, instalado en `C:\VulkanSDK` |
| Device Vulkan | llvmpipe (lavapipe) | Software: la GPU Intel de esta máquina no tiene driver Vulkan. **Mesa 26.2.4** (26.2.3 tiene heap corruption, ver `docs/entorno.md`) |

## Build

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cmake -S . -B build
cmake --build build --config Release
```

El ejecutable queda en `build\MotorSK.exe`.

## Estructura

```
Motor-SK/
├── CMakeLists.txt      # build (Vulkan + glslc para shaders)
├── shaders/            # GLSL (se compilan a SPIR-V en cada build)
├── src/
│   ├── core/           # logging
│   ├── math/           # vectores y matrices propios (estilo glm)
│   ├── platform/       # ventana Win32 e input (capa portable)
│   ├── scene/          # cámara libre
│   ├── render/         # renderer Vulkan (swapchain, pipeline, rejilla)
│   └── main.cpp        # bucle principal
└── docs/               # documentación
```

## Documentación

- [docs/entorno.md](docs/entorno.md): instalación exacta del entorno (replicable en otra máquina)
- [docs/roadmap.md](docs/roadmap.md): fases del motor
