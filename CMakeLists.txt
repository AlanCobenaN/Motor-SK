cmake_minimum_required(VERSION 3.20)

project(MotorSK
    VERSION 0.1.0
    DESCRIPTION "Motor de juegos 3D en Vulkan"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Recursos de Windows (app.rc): icono del .exe. Solo con MSVC, que trae
    src/ui/docs_view.cpp
    src/ui/docs_view.h
# rc.exe; CMake compila los .rc listados en las fuentes del target.
if(MSVC)
    enable_language(RC)
endif()

# El Vulkan SDK de LunarG define VULKAN_SDK al instalar; si la sesion no la
# tiene (terminales nuevas), se usa la ruta por defecto como hint.
if(NOT DEFINED ENV{VULKAN_SDK} AND EXISTS "C:/VulkanSDK")
    set(ENV{VULKAN_SDK} "C:/VulkanSDK")
endif()

find_package(Vulkan REQUIRED)

add_executable(${PROJECT_NAME}
    app.rc
    src/ui/docs_view.cpp
    src/ui/docs_view.h
    src/core/config.cpp
    src/platform/window.cpp
    src/project/project.cpp
    src/project/scripts.cpp
    src/scene/scene.cpp
    src/scene/picking.cpp
    src/scene/gizmo.cpp
    src/scene/snapping.cpp
    src/ui/code_view.cpp
    src/render/renderer.cpp
    src/render/meshes.cpp
    src/ui/projects_panel.cpp
    src/ui/dark_button.cpp
    src/ui/prompt.cpp
    src/ui/script_picker.cpp
    src/ui/place_view.cpp
    src/ui/workspace_tabs.cpp
)

# Para que rc.exe encuentre logo.ico junto al app.rc.
    src/ui/docs_view.cpp
    src/ui/docs_view.h

target_compile_definitions(${PROJECT_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)

# comctl32: ListView; shell32: SHGetFolderPathA (Documentos);
# gdi32: pintado de botones; dwmapi: barra de titulo oscura.
target_link_libraries(${PROJECT_NAME} PRIVATE Vulkan::Vulkan comctl32 shell32 gdi32 dwmapi)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    # Aplicacion de ventana: sin consola cmd detras (los logs salen solo
    # si se redirige la salida: MotorSK.exe > log.txt).
    target_link_options(${PROJECT_NAME} PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# --- Shaders: glslc (del Vulkan SDK) compila GLSL a SPIR-V en cada build ---
find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin")
if(NOT GLSLC)
    message(FATAL_ERROR "glslc no encontrado; instala el Vulkan SDK o define VULKAN_SDK")
endif()

set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BIN_DIR})

add_custom_command(
    OUTPUT
        ${SHADER_BIN_DIR}/grid.spv
        ${SHADER_BIN_DIR}/grid.frag.spv
    COMMAND ${GLSLC} -O ${SHADER_SRC_DIR}/grid.vert -o ${SHADER_BIN_DIR}/grid.spv
    COMMAND ${GLSLC} -O ${SHADER_SRC_DIR}/grid.frag -o ${SHADER_BIN_DIR}/grid.frag.spv
    DEPENDS ${SHADER_SRC_DIR}/grid.vert ${SHADER_SRC_DIR}/grid.frag
    COMMENT "Compilando shaders SPIR-V"
)

add_custom_target(compile_shaders
    DEPENDS ${SHADER_BIN_DIR}/grid.spv ${SHADER_BIN_DIR}/grid.frag.spv
)
add_dependencies(${PROJECT_NAME} compile_shaders)

# Los shaders se cargan junto al .exe en tiempo de ejecucion.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SHADER_BIN_DIR}
            $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMENT "Copiando shaders al directorio del ejecutable"
)
