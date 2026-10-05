# Roadmap

## Fase 0 — Setup ✅

- [x] Compilador MSVC 19.44 + CMake 4.4.4
- [x] Vulkan SDK 1.4.363.0
- [x] ICD de software (lavapipe) para poder ejecutar sin GPU Vulkan
- [x] Repositorio con build base

## Fase 1 — Ventana y cámara (en curso)

Ejecutable `.exe` con:

- [x] Ventana Win32 con swapchain de Vulkan (resize incluido)
- [x] Panel de proyectos (Win32 ListView): listar, crear, abrir, renombrar,
      borrar y lista de recientes. Raíz fija: `Documentos\Motor SK\Proyectos`
- [x] Interfaz modo oscuro: fondo negro, texto blanco, botones redondeados
      (owner-draw), lista y cabeceras oscuras, barra de título oscura
- [x] Aplicación de ventana sin consola (errores fatales en MessageBox)
- [x] Tabla con márgenes, esquinas redondeadas y marco; tipografía Segoe UI
      (16px, cabeceras semibold)
- [x] Abrir un proyecto → vista 3D (Esc vuelve al panel)
- [x] Divisiones del workspace en la vista 3D: banda superior de altura fija
       con Objetos / Scripts / GUI (botones redondeados, oscuro, texto blanco)
       y un area de trabajo debajo; por ahora vacias (solo la division)
- [ ] Abrir un archivo/archivo de escena
- [x] Cámara libre: WASD para moverse, click derecho para mirar, scroll para
      acercar/alejar
- [x] Rejilla de referencia en el suelo (para percibir el movimiento)

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
