# Roadmap

## Fase 0 — Setup ✅

- [x] Compilador MSVC 19.44 + CMake 4.4.4
- [x] Vulkan SDK 1.4.363.0
- [x] ICD de software (lavapipe) para poder ejecutar sin GPU Vulkan
- [x] Repositorio con build base

## Fase 1 — Ventana y cámara (en curso)

Ejecutable `.exe` con:

- [ ] Ventana Win32 con swapchain de Vulkan
- [ ] Panel simple con la lista de proyectos (carpeta de proyectos)
- [ ] Abrir un archivo/archivo de escena
- [ ] Cámara libre: WASD para moverse, click derecho para mirar, scroll para
      acercar/alejar
- [ ] Rejilla de referencia en el suelo (para percibir el movimiento)

## Fase 2 — Render básico

- [ ] Pipelines: shader vert/frag con `glslc` en build time
- [ ] Mallas (triángulo/cubo) y materiales simples
- [ ] Depth buffer

## Fase 3 — Escenas y recursos

- [ ] Formato de escena propio (texto, versionado)
- [ ] Carga de recursos (mallas, texturas)

## Futuro

- [ ] Backend Linux (X11/Wayland) trasladando la capa `platform/`
- [ ] Android via NDK (clang) reutilizando la capa `platform/` y el renderer
