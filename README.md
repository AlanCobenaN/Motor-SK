# Motor SK

Motor de juegos 3D escrito en C++20 con Vulkan, enfocado en Windows hoy y con
rutas de portabilidad a Linux y Android.

## Estado actual

Fase: **1 — ventana y cámara** (en curso). El `.exe` arranca en un **panel de
proyectos** (Win32 nativo) donde puedes crear, abrir, renombrar y borrar
proyectos; al abrir uno entra la vista 3D con rejilla y cámara libre
(WASD + click derecho para mirar + scroll para zoom). **Esc** vuelve al panel.

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

Flags para pruebas:

- `--frames N`: sale tras N frames (smoke test sin interacción)
- `--proyecto <ruta>`: arranca directamente en la vista 3D de ese proyecto

## Proyectos

- Raíz: `Documentos\Motor SK\Proyectos` (se crea sola en el primer arranque)
- Un proyecto = carpeta con `proyecto.sk` (texto `clave: valor`):

```
nombre: Mi Juego
version: 1
escena: escenas/inicio.scene
```

- Recientes (últimas 5): `%APPDATA%\MotorSK\config.txt`

## Estructura

```
Motor-SK/
├── CMakeLists.txt      # build (Vulkan + glslc para shaders)
├── shaders/            # GLSL (se compilan a SPIR-V en cada build)
├── src/
│   ├── core/           # logging + config (%APPDATA%, recientes)
│   ├── math/           # vectores y matrices propios (estilo glm)
│   ├── platform/       # ventana Win32 e input (capa portable)
│   ├── project/        # modelo de proyecto (proyecto.sk, CRUD de carpetas)
│   ├── scene/          # cámara libre
│   ├── render/         # renderer Vulkan (swapchain, pipeline, rejilla)
│   ├── ui/             # panel de proyectos (Win32 ListView)
│   └── main.cpp        # bucle principal (menú ↔ vista 3D)
└── docs/               # documentación
```

## Documentación

- [docs/entorno.md](docs/entorno.md): instalación exacta del entorno (replicable en otra máquina)
- [docs/roadmap.md](docs/roadmap.md): fases del motor
