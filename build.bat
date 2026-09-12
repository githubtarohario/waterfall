@echo off
rem =====================================================================
rem  build.bat  --  Build with MSVC (Visual Studio 2022 / 18)
rem    build.bat        : release build -> build\SPHSplash.exe
rem    build.bat debug  : debug build
rem  At runtime the exe looks for ..\shaders\ relative to the build dir.
rem  (Comments are ASCII only: cmd.exe cannot parse UTF-8 Japanese.)
rem =====================================================================
setlocal
set VCVARS=
for %%V in (18 2022) do (
    if exist "C:\Program Files\Microsoft Visual Studio\%%V\Community\VC\Auxiliary\Build\vcvars64.bat" (
        if not defined VCVARS set "VCVARS=C:\Program Files\Microsoft Visual Studio\%%V\Community\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if not defined VCVARS (
    echo vcvars64.bat not found. Install Visual Studio with C++ tools.
    exit /b 1
)
call "%VCVARS%" >nul

cd /d "%~dp0"
if not exist build mkdir build

set CFLAGS=/nologo /utf-8 /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE
if /i "%1"=="debug" (
    set CFLAGS=%CFLAGS% /Od /Zi /MDd /D_DEBUG
) else (
    set CFLAGS=%CFLAGS% /O2 /MD /DNDEBUG
)

rem --- main application ---
cl %CFLAGS% /Fo:build\ /Fe:build\SPHSplash.exe ^
    src\main.cpp src\D3DUtil.cpp src\SPHSimulator.cpp src\MarchingCubes.cpp src\MCTables.cpp src\Renderer.cpp ^
    /link /SUBSYSTEM:CONSOLE user32.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1

rem --- marching cubes table unit test ---
cl %CFLAGS% /Fo:build\ /Fe:build\mc_selftest.exe src\mc_selftest.cpp src\MCTables.cpp
if errorlevel 1 exit /b 1

echo.
echo Build finished: build\SPHSplash.exe
endlocal
