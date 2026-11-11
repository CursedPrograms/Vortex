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
rem Microsoft's Visual C++ runtime goes next to the exe, so it starts on PCs without the redistributable installed
set "CRT="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -find VC\Redist\MSVC\*\x64\Microsoft.VC143.CRT`) do set "CRT=%%i"
if not defined CRT echo Visual C++ runtime folder not found, is Visual Studio 2022 installed?& goto :error
for %%f in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_1.dll) do copy /y "%CRT%\%%f" dist\Vortex\ >nul || goto :error
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
