<#
    Empaqueta Brújula en un único ejecutable instalador: Instalador-Brujula.exe.

    1. Prepara el entorno de compilación (MSVC, CMake, Ninja).
    2. Compila brujula.exe en modo Release.
    3. Compila el instalador autónomo incrustando brujula.exe.
    4. Deja el instalador final en build\Instalador-Brujula.exe.

        powershell -NoProfile -ExecutionPolicy Bypass -File empaquetar.ps1
#>
[CmdletBinding()]
param(
    [string]$Directorio = 'build'
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "   Empaquetador de Brújula (Instalador Nativo)    " -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

# 1. Configurar el entorno con preparar.ps1
Write-Host "`n[1/4] Configurando entorno de compilación..." -ForegroundColor Yellow
. .\preparar.ps1 -SoloEntorno

# 2. Configurar CMake si no está configurado
if (-not (Test-Path "$Directorio\build.ninja")) {
    Write-Host "`n[2/4] Generando proyecto con CMake (Release)..." -ForegroundColor Yellow
    cmake -S . -B $Directorio -G Ninja -DCMAKE_BUILD_TYPE=Release
}

# 3. Compilar brujula.exe primero
Write-Host "`n[3/4] Compilando Brújula (Release)..." -ForegroundColor Yellow
cmake --build $Directorio --config Release --target brujula

if ($LASTEXITCODE -ne 0) {
    throw "Falló la compilación de brujula.exe"
}

# 4. Compilar brujula_installer
Write-Host "`n[4/4] Compilando Instalador-Brujula.exe..." -ForegroundColor Yellow
cmake --build $Directorio --config Release --target brujula_installer

if ($LASTEXITCODE -ne 0) {
    throw "Falló la compilación del instalador"
}

$instalador = Join-Path $Directorio "Instalador-Brujula.exe"
if (Test-Path $instalador) {
    $pesoMB = [Math]::Round((Get-Item $instalador).Length / 1MB, 2)
    Write-Host "`n==================================================" -ForegroundColor Green
    Write-Host " ¡Instalador generado con éxito!" -ForegroundColor Green
    Write-Host " Archivo : $instalador ($pesoMB MB)" -ForegroundColor Green
    Write-Host "==================================================" -ForegroundColor Green
} else {
    throw "No se encontró el instalador generado en $instalador"
}
