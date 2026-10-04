# Entorno de desarrollo

Instalaciones exactas de esta máquina (Windows 10/11, sin GPU Vulkan).
Útiles para replicar el setup en otra PC.

## 1. Compilador: VS 2022 Build Tools

- `vs_BuildTools.exe` de https://aka.ms/vs/17/release/vs_BuildTools.exe
- Workload: `Microsoft.VisualStudio.Workload.VCTools` con `--includeRecommended`
- Instalación en `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`
- Verificación: `cl.exe` 19.44.35229 (x64)

Entorno para compilar:

```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
```

## 2. CMake 4.4.4 (portable)

- Zip oficial de Kitware (sin instalador, sin admin)
- Instalado en `C:\Users\alanj\AppData\Local\Programs\CMake\bin` (agregado al PATH de usuario)

## 3. Vulkan SDK 1.4.363.0

- Instalador de LunarG (`https://sdk.lunarg.com/sdk/download/latest/windows/vulkan_sdk.exe`),
  SHA-256 verificado contra `sdk.lunarg.com/sdk/sha/`
- Instalado en `C:\VulkanSDK`
- Trae: loader en `C:\Windows\System32\vulkan-1.dll`, capas de validación,
  `glslc`/`glslangValidator` para shaders

## 4. ICD de software: lavapipe (Mesa)

La GPU de esta máquina es **Intel HD Graphics (Broadwell GT2, driver 2015)** y no
tiene driver Vulkan en Windows, así que se instaló un ICD de CPU:

- Paquete: `mesa3d-26.2.3-release-msvc.7z` de
  https://github.com/pal1000/mesa-dist-win/releases (SHA-256 verificado)
- `vulkan_lvp.dll` copiado a `C:\Windows\System32`
- Manifest `C:\Windows\System32\vk_lvp_icd.json` (api_version 1.4.354)
- Registrado en `HKLM\SOFTWARE\Khronos\Vulkan\Drivers`
  (nombre = ruta del json con backslashes simples, DWORD = 0)

Verificación: crear instancia Vulkan y enumerar dispositivos → aparece
`llvmpipe (LLVM 23.1.2)`, tipo `VK_PHYSICAL_DEVICE_TYPE_CPU`.

Con una GPU real (NVIDIA/AMD/Intel con driver Vulkan) este ICD convive sin
conflicto: el loader expone ambos dispositivos.

## Nota: `vkcube` colgado

`vkcube.exe` del SDK se queda sin salir (incluso con `--help`) en esta máquina.
No se usó como prueba; la verificación se hace con el propio código del motor.
