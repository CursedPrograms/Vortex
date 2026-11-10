# Builds VORTEX (Release) into build\Release, and dist\Vortex-win64.zip.
# Needs Visual Studio 2022 (C++), CMake. SDL2's development package is fetched into external\ once.
# Native tools are checked by their exit codes: CMake prints harmless warnings on stderr, which "Stop" would treat as
# failures in Windows PowerShell 5.1.
Set-Location $PSScriptRoot
function Run { & $args[0] $args[1..($args.Count - 1)]; if ($LASTEXITCODE) { Write-Host "Build failed, see above."; exit 1 } }
if (-not (Test-Path "external\SDL2-2.30.9\cmake")) {
    Write-Host "Getting SDL2's development package, once..."
    New-Item -ItemType Directory -Force external | Out-Null
    Invoke-WebRequest https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip -OutFile external\SDL2-devel.zip
    Expand-Archive external\SDL2-devel.zip -DestinationPath external -Force
    Remove-Item external\SDL2-devel.zip
}
Run cmake -B build -G "Visual Studio 17 2022" -A x64
Run cmake --build build --config Release
Remove-Item -Recurse -Force dist\Vortex -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force dist\Vortex | Out-Null
Copy-Item -Recurse build\Release\* dist\Vortex\
Remove-Item dist\Vortex\*.lib -ErrorAction SilentlyContinue
Copy-Item ..\README.md, ..\LICENSE dist\Vortex\
Remove-Item dist\Vortex-win64.zip -ErrorAction SilentlyContinue
# Windows' own tar makes a zip with forward slashes, which every unzip tool reads
Run tar -a -c -f dist\Vortex-win64.zip -C dist\Vortex *
Write-Host "Built: build\Release\Vortex.exe and dist\Vortex-win64.zip"
