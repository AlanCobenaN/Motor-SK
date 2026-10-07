@echo off
rem Compila MotorSK en Release (MSVC + CMake + Vulkan SDK).
rem Uso: build.bat  [opciones extra de cmake se ignoran; build limpio: build.bat clean]
setlocal

set "VULKAN_SDK=C:\VulkanSDK\1.4.363.0"
if not exist "%VULKAN_SDK%" (
    for /d %%D in ("C:\VulkanSDK\*") do set "VULKAN_SDK=%%~fD"
)
set "PATH=%LOCALAPPDATA%\Programs\CMake\bin;%VULKAN_SDK%\Bin;%PATH%"

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo [ERROR] No se pudo cargar el entorno de MSVC.
    exit /b 1
)

cd /d "%~dp0"

if /I "%~1"=="clean" (
    if exist build rmdir /s /q build
)

cmake -S . -B build
if errorlevel 1 exit /b 1

cmake --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo Listo: build\Release\MotorSK.exe
endlocal
