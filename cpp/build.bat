@echo off
setlocal
cd /d "%~dp0"
rem Builds VORTEX (Release) into build\Release, and dist\Vortex-win64.zip.
rem Needs Visual Studio 2022 (C++), CMake. SDL2's development package is fetched into external\ once.
if not exist "external\SDL2-2.30.9\cmake" (
    echo Getting SDL2's development package, once...
    if not exist external mkdir external
    curl -sL -o external\SDL2-devel.zip https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip || goto :error
    tar -xf external\SDL2-devel.zip -C external || goto :error
    del external\SDL2-devel.zip
)
cmake -B build -G "Visual Studio 17 2022" -A x64 || goto :error
cmake --build build --config Release || goto :error
if exist dist\Vortex rmdir /s /q dist\Vortex
mkdir dist\Vortex
xcopy /e /i /q /y build\Release\* dist\Vortex\ >nul
del /q dist\Vortex\*.lib 2>nul
copy /y ..\README.md dist\Vortex\ >nul
copy /y ..\LICENSE dist\Vortex\ >nul
if exist dist\Vortex-win64.zip del dist\Vortex-win64.zip
rem Windows' own tar makes a zip with forward slashes, which every unzip tool reads
tar -a -c -f dist\Vortex-win64.zip -C dist\Vortex * || goto :error
echo Built: build\Release\Vortex.exe and dist\Vortex-win64.zip
exit /b 0
:error
echo Build failed, see above.
pause
exit /b 1
