<#
    Prepara el entorno de desarrollo y dependencias para Isla en Windows.
    Descarga e instala el SDK de .NET 10 en el perfil de usuario (%LOCALAPPDATA%\Microsoft\dotnet)
    si no está presente en la máquina, sin requerir privilegios de administrador.
    Restaura también los paquetes NuGet requeridos por el proyecto.

    Uso:
        powershell -NoProfile -ExecutionPolicy Bypass -File preparar.ps1
#>

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

Write-Host "Comprobando entorno de .NET 10 para Isla..." -ForegroundColor Cyan

function Obtener-Dotnet10 {
    # 1. Comprobar si 'dotnet' en el PATH actual tiene el SDK 10
    $cmd = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($cmd) {
        $sdks = & $cmd.Source --list-sdks 2>$null
        if ($sdks -like '*10.*') {
            return $cmd.Source
        }
    }

    # 2. Comprobar si ya está instalado en el perfil de usuario
    $localDotnet = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'
    if (Test-Path $localDotnet) {
        $sdks = & $localDotnet --list-sdks 2>$null
        if ($sdks -like '*10.*') {
            return $localDotnet
        }
    }

    return $null
}

$dotnetExe = Obtener-Dotnet10

if (-not $dotnetExe) {
    Write-Host "No se encontró el SDK de .NET 10. Descargando instalador oficial de Microsoft..." -ForegroundColor Yellow
    $installScript = Join-Path $env:TEMP 'dotnet-install.ps1'

    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13
    Invoke-WebRequest -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $installScript -UseBasicParsing

    Write-Host "Instalando .NET 10 SDK en $($env:LOCALAPPDATA)\Microsoft\dotnet..." -ForegroundColor Cyan
    & $installScript -Channel 10.0

    if (Test-Path $installScript) {
        Remove-Item $installScript -Force
    }

    # Configurar variables de usuario en el registro
    $dotnetDir = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet'
    [Environment]::SetEnvironmentVariable('DOTNET_ROOT', $dotnetDir, 'User')
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if ($userPath -notlike "*$dotnetDir*") {
        [Environment]::SetEnvironmentVariable('Path', "$dotnetDir;$userPath", 'User')
    }

    # Actualizar variables de la sesión actual
    $env:DOTNET_ROOT = $dotnetDir
    $env:PATH = "$dotnetDir;$env:PATH"

    $dotnetExe = Obtener-Dotnet10
    if (-not $dotnetExe) {
        throw "La instalación de .NET 10 SDK no pudo completarse."
    }
}

Write-Host "SDK de .NET 10 detectado: $dotnetExe" -ForegroundColor Green
& $dotnetExe --version

# Restaurar paquetes y dependencias del proyecto
if (Test-Path 'Isla.csproj') {
    Write-Host "Restaurando paquetes NuGet y generadores de código..." -ForegroundColor Cyan
    & $dotnetExe restore Isla.csproj
    Write-Host "Dependencias restauradas correctamente." -ForegroundColor Green
}

Write-Host "Todo listo. Ya puedes ejecutar: powershell -ExecutionPolicy Bypass -File .\instalar.ps1" -ForegroundColor Green
