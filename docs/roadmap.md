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
- [x] Icono del programa (logo.ico embebido via app.rc): ventana, barra de
       tareas y Alt+Tab
- [x] Banda superior en dos filas (72px): barra de atajos estilo Roblox con
       el boton "Part" (anade un objeto 3D a la escena) y navbar compacta con
       PLACE / CODE / GUI (botones redondeados, oscuro, texto blanco);
       sin area vacia, la vista 3D ocupa el resto
- [x] Swapchain se recrea en cada resize (WM_SIZE avisa al renderer; sin
       depender de OUT_OF_DATE, que lavapipe no devuelve)
- [x] Division PLACE: paneles Properties (Transform del objeto seleccionado)
       a la izquierda y Explorer (arbol con raiz unica `dimension01` y los
       objetos Part insertados) a la derecha; visibles solo en PLACE,
       ocultos en CODE/GUI
- [x] Division CODE: organizador de scripts (Server/Shared/Player/Character),
       carpetas anidadas, crear script/carpeta, renombrar, borrar con
       confirmacion, raices protegidas, Properties con Nombre/Tipo/Ubicacion
       a la izquierda y Scripts a la derecha; visibles solo en CODE
- [x] Objetos 3D "Part": escena propia (Scene/SceneObject con posicion,
       rotacion y escala), el boton Part inserta un cubo sobre la rejilla,
       Explorer anade y selecciona el nodo, Properties refresca el Transform
- [x] ModelScript: asociar scripts del organizador con objetos de la escena:
       fila "Asociado a" en PLACE Properties con boton "Cambiar..." que abre
       un selector modal de scripts, el CODE muestra el objeto asociado en su
       Properties, borrar/renombrar scripts remapea (y guarda) las
       asociaciones, y todo persiste con la escena
- [ ] Abrir un archivo/archivo de escena
- [x] Cámara libre: WASD para moverse, click derecho para mirar, scroll para
       acercar/alejar
- [x] Rejilla de referencia en el suelo (para percibir el movimiento)

## Fase 2 — Render básico

- [ ] Pipelines: shader vert/frag con `glslc` en build time
- [x] Mallas: cubo de 36 vertices con color por cara (pipeline de
       triangulos propio, mismos shaders y push constant que la rejilla)
- [x] Depth buffer

## Fase 3 — Escenas y recursos

- [x] Formato de escena propio (texto, versionado): `escenas/inicio.scene`
       con `objeto/posicion/rotacion/escala/script`, version: 1, tolerante a
       claves desconocidas; se carga al abrir el proyecto y se guarda en cada
       mutacion (Part, asociacion, renombrar/borrar scripts)
- [ ] Carga de recursos (mallas, texturas)

## Futuro

- [ ] Backend Linux (X11/Wayland) trasladando la capa `platform/`
- [ ] Android via NDK (clang) reutilizando la capa `platform/` y el renderer
