$ErrorActionPreference = "Stop"
$gpp = "C:\msys64\mingw32\bin\g++.exe"
if (-not (Test-Path $gpp)) {
    throw "No se encontró el compilador 32-bit en $gpp"
}

$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$env:PATH = "C:\msys64\mingw32\bin;" + $env:PATH

Write-Host "Compilando WE_DarkMode.dll (x86 32-bit)..."
& $gpp -O2 -shared -std=c++17 -m32 -static-libgcc -static-libstdc++ "-Wl,-Bstatic" -lwinpthread "-Wl,-Bdynamic" `
    "$dir\we_darkmode_dll.cpp" `
    -o "$dir\WE_DarkMode.dll" `
    -luser32 -lgdi32 -lcomctl32 -luxtheme -ldwmapi -lpsapi -lshlwapi `
    "-Wl,--kill-at"

if ($LASTEXITCODE -ne 0) { throw "Error compilando WE_DarkMode.dll" }

Write-Host "Compilando WorldEditor_Dark.exe (x86 32-bit)..."
& $gpp -O2 -mwindows -municode -std=c++17 -m32 -static `
    "$dir\launcher.cpp" `
    -o "$dir\WorldEditor_Dark.exe" `
    -luser32 -lcomdlg32 -lshell32 -lshlwapi

if ($LASTEXITCODE -ne 0) { throw "Error compilando WorldEditor_Dark.exe" }

$verDir = Join-Path $dir "WC3-Classic-1.21-1.27b"
$verSub = Join-Path $verDir "WE_DarkMode"
New-Item -ItemType Directory -Force -Path $verSub | Out-Null
Copy-Item "$dir\WorldEditor_Dark.exe" (Join-Path $verDir "WorldEditor_Dark.exe") -Force
Copy-Item "$dir\WE_DarkMode.dll"      (Join-Path $verSub "WE_DarkMode.dll")      -Force
Copy-Item "$dir\WE_DarkMode.ini"      (Join-Path $verSub "WE_DarkMode.ini")      -Force

Write-Host "Listo: WE_DarkMode.dll y WorldEditor_Dark.exe compilados y empaquetados en WC3-Classic-1.21-1.27b."
