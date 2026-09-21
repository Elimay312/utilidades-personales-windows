<#
    Instalación por usuario: publica el exe a %LOCALAPPDATA%\Dock\app, deja un acceso
    directo en el menú Inicio y sincroniza el autoarranque. Todo bajo HKCU: no pide
    administrador y no toca la carpeta Inicio del sistema ni el programador de tareas
    (SEGURIDAD.md, el autoarranque es HKCU\...\Run y nada más).

        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1
        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -SinAutoArranque
        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -Desinstalar

    Hace falta el SDK de .NET 10 en la máquina de destino. Si no existe, se prepara
    e instala automáticamente en el perfil de usuario sin requerir elevación.
#>
param(
    [switch]$SinAutoArranque,
    [switch]$Desinstalar
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$raiz    = Join-Path $env:LOCALAPPDATA 'Dock'
$destino = Join-Path $raiz 'app'
$exe     = Join-Path $destino 'Dock.exe'
$config  = Join-Path $raiz 'dock.json'
$acceso  = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Dock.lnk'
$run     = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'

# Solo estorba la copia INSTALADA: si se deja viva, publish falla al reemplazar archivos.
function Parar {
    $procesos = Get-Process Dock -ErrorAction SilentlyContinue
    foreach ($p in $procesos) {
        $esMio = $false
        try {
            if ($p.Path -eq $exe -or $p.MainModule.FileName -eq $exe) { $esMio = $true }
        } catch {
            # Si no se puede leer la ruta exacta pero coincide el nombre, se evalúa
        }
        if ($esMio) {
            $p | Stop-Process -Force
        }
    }
    Start-Sleep -Milliseconds 400
}

if ($Desinstalar) {
    Parar
    # Quitar valor de Run para no dejar una entrada muerta en el Administrador de tareas.
    Remove-ItemProperty -Path $run -Name 'Dock' -ErrorAction SilentlyContinue
    if (Test-Path $acceso)  { Remove-Item $acceso -Force }
    if (Test-Path $destino) { Remove-Item $destino -Recurse -Force }
    Write-Host "Dock desinstalado." -ForegroundColor Green
    # La configuración no se borra: es del usuario, no del programa.
    Write-Host "Tu configuración (dock.json) se mantiene en $raiz."
    return
}

Parar

# Detección y resolución de .NET 10 SDK
$dotnet = 'dotnet'
$userDotnet = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'

function Probar-Sdk10($cmd) {
    if (-not $cmd) { return $false }
    try {
        $sdks = & $cmd --list-sdks 2>$null
        return ($sdks -like '*10.*')
    } catch {
        return $false
    }
}

if (Probar-Sdk10 $dotnet) {
    # dotnet en PATH ya cuenta con SDK 10
} elseif ((Test-Path $userDotnet) -and (Probar-Sdk10 $userDotnet)) {
    $dotnet = $userDotnet
    $dotnetDir = Split-Path $userDotnet
    $env:DOTNET_ROOT = $dotnetDir
    $env:PATH = "$dotnetDir;$env:PATH"
} else {
    $preparar = Join-Path $PSScriptRoot 'preparar.ps1'
    if (Test-Path $preparar) {
        Write-Host "SDK de .NET 10 o paquetes no detectados. Preparando automáticamente..." -ForegroundColor Yellow
        & $preparar
        if ((Test-Path $userDotnet) -and (Probar-Sdk10 $userDotnet)) {
            $dotnet = $userDotnet
            $dotnetDir = Split-Path $userDotnet
            $env:DOTNET_ROOT = $dotnetDir
            $env:PATH = "$dotnetDir;$env:PATH"
        }
    }
}

if (-not (Probar-Sdk10 $dotnet)) {
    throw "No se encontró el SDK de .NET 10 necesario para compilar e instalar Dock."
}

# Restaurar paquetes de NuGet por si no se han restaurado
& $dotnet restore Dock.csproj
if ($LASTEXITCODE -ne 0) { throw "dotnet restore falló al restaurar paquetes de Dock.csproj." }

# Publicar el ejecutable en %LOCALAPPDATA%\Dock\app
Write-Host "Publicando Dock en $destino..." -ForegroundColor Cyan
& $dotnet publish -c Release -o $destino Dock.csproj
if ($LASTEXITCODE -ne 0) { throw "dotnet publish falló. Hace falta el SDK de .NET 10." }

# Sembrar dock.json si no existe aún en %LOCALAPPDATA%\Dock\
if (-not (Test-Path $config)) {
    New-Item -ItemType Directory -Force $raiz | Out-Null
    $seed = Join-Path $PSScriptRoot 'dock.json'
    if (Test-Path $seed) {
        Copy-Item $seed $config
    } elseif (Test-Path (Join-Path $destino 'dock.json')) {
        Copy-Item (Join-Path $destino 'dock.json') $config
    }
}

# Ajustar autoStart en dock.json si se especifica la opción
if (Test-Path $config) {
    $antes = Get-Content $config -Raw
    if ($SinAutoArranque) {
        $despues = $antes -replace '("autoStart"\s*:\s*)true', '${1}false'
        if ($despues -ne $antes) {
            Set-Content $config $despues -Encoding UTF8 -NoNewline
        }
        Remove-ItemProperty -Path $run -Name 'Dock' -ErrorAction SilentlyContinue
    } else {
        $despues = $antes -replace '("autoStart"\s*:\s*)false', '${1}true'
        if ($despues -ne $antes) {
            Set-Content $config $despues -Encoding UTF8 -NoNewline
        }
    }
}

# Crear acceso directo en el Menú Inicio con WScript.Shell
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($acceso)
$link.TargetPath = $exe
$link.WorkingDirectory = $destino
$link.Description = 'Dock estilo macOS para Windows 11'
$link.Save()

# Arrancar Dock
Start-Process $exe

Write-Host "Dock instalado correctamente en $destino y arrancado." -ForegroundColor Green
Write-Host "Configuración: $config (se recarga sola al guardarla)."
if ($SinAutoArranque) {
    Write-Host "Sin autoarranque. Para encenderlo: pon autoStart en true en dock.json."
} else {
    Write-Host "Autoarranque encendido (visible en la pestaña Inicio del Administrador de tareas)."
}
Write-Host "Para quitarlo: powershell -ExecutionPolicy Bypass -File .\instalar.ps1 -Desinstalar"
