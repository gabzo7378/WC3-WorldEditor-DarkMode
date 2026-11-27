$ErrorActionPreference = "Stop"
$gpp     = "C:\msys64\mingw32\bin\g++.exe"
$windres = "C:\msys64\mingw32\bin\windres.exe"
if (-not (Test-Path $gpp)) {
    throw "No se encontro el compilador 32-bit en $gpp"
}

$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$env:PATH = "C:\msys64\mingw32\bin;" + $env:PATH

Write-Host "Compilando recursos de icono y version (Clasico 5 HD + JNGP 2 HD)..."
& $windres "$dir\launcher.rc" -O coff -o "$dir\launcher_res.o"
if ($LASTEXITCODE -ne 0) { throw "Error compilando launcher.rc" }

& $windres "$dir\jngp_launcher.rc" -O coff -o "$dir\jngp_launcher_res.o"
if ($LASTEXITCODE -ne 0) { throw "Error compilando jngp_launcher.rc" }

Write-Host "Compilando WE_DarkMode.dll (x86 32-bit - Classic + JNGP TESH/Scintilla)..."
& $gpp -O2 -shared -std=c++17 -m32 -fno-exceptions -fno-rtti -static-libgcc -static-libstdc++ `
    "$dir\we_darkmode_dll.cpp" `
    -o "$dir\WE_DarkMode.dll" `
    -luser32 -lgdi32 -lcomctl32 -luxtheme -ldwmapi -lpsapi -lshlwapi `
    "-Wl,-Bstatic" -l:libwinpthread.a "-Wl,-Bdynamic" `
    "-Wl,--kill-at"

if ($LASTEXITCODE -ne 0) { throw "Error compilando WE_DarkMode.dll" }

Write-Host "Compilando WorldEditor_Dark.exe (x86 32-bit - icono Clasico 5 HD)..."
& $gpp -O2 -mwindows -municode -std=c++17 -m32 -static `
    "$dir\launcher.cpp" "$dir\launcher_res.o" `
    -o "$dir\WorldEditor_Dark.exe" `
    -luser32 -lcomdlg32 -lshell32 -lshlwapi

if ($LASTEXITCODE -ne 0) { throw "Error compilando WorldEditor_Dark.exe" }

Write-Host "Compilando NewGen_WE_Dark.exe (x86 32-bit ligero sin inyeccion - icono JNGP 2 HD)..."
& $gpp -O2 -mwindows -municode -std=c++17 -m32 -static-libgcc `
    "$dir\jngp_launcher.cpp" "$dir\jngp_launcher_res.o" `
    -o "$dir\NewGen_WE_Dark.exe" `
    -luser32 -lshell32 -lshlwapi

if ($LASTEXITCODE -ne 0) { throw "Error compilando NewGen_WE_Dark.exe" }

# 1. Empaquetar WC3-Classic-1.21-1.27b
$verClassic    = Join-Path $dir "WC3-Classic-1.21-1.27b"
$verClassicSub = Join-Path $verClassic "WE_DarkMode"
New-Item -ItemType Directory -Force -Path $verClassicSub | Out-Null
Copy-Item "$dir\WorldEditor_Dark.exe" (Join-Path $verClassic "WorldEditor_Dark.exe")   -Force
Copy-Item "$dir\WE_DarkMode.dll"      (Join-Path $verClassicSub "WE_DarkMode.dll")     -Force
Copy-Item "$dir\WE_DarkMode.ini"      (Join-Path $verClassicSub "WE_DarkMode.ini")     -Force

# 2. Empaquetar WC3-JNGP
$verJngp    = Join-Path $dir "WC3-JNGP"
$verJngpSub = Join-Path $verJngp "WE_DarkMode"
New-Item -ItemType Directory -Force -Path $verJngpSub | Out-Null
Copy-Item "$dir\NewGen_WE_Dark.exe"   (Join-Path $verJngp "NewGen_WE_Dark.exe")        -Force
Copy-Item "$dir\WE_DarkMode.dll"      (Join-Path $verJngpSub "WE_DarkMode_JNGP.dll")   -Force

# 3. Verificacion automatica: cero DLLs externas de MinGW (solo DLLs nativas de Windows)
$objdump = "C:\msys64\mingw32\bin\objdump.exe"
foreach ($bin in @(
    (Join-Path $verClassic "WorldEditor_Dark.exe"),
    (Join-Path $verClassicSub "WE_DarkMode.dll"),
    (Join-Path $verJngp "NewGen_WE_Dark.exe"),
    (Join-Path $verJngpSub "WE_DarkMode_JNGP.dll")
)) {
    $bad = & $objdump -p $bin | Select-String -Pattern "DLL Name:.*(libgcc|libstdc|libwinpthread|msys)"
    if ($bad) { throw "Dependencia externa prohibida en $bin : $bad" }
}

Write-Host "Listo: WC3-Classic-1.21-1.27b y WC3-JNGP compilados, verificados y empaquetados."
