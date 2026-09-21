<#
    Instalacion por usuario: copia el exe a %LOCALAPPDATA%\Programs\Rayo y deja un acceso
    directo en el menu Inicio, para que Rayo salga en la busqueda y se pueda anclar. No
    pide administrador y no toca el registro.

        powershell -ExecutionPolicy Bypass -File install.ps1
        powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
#>
param(
    [string]$Source = "$PSScriptRoot\build\rayo.exe",
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

$target   = Join-Path $env:LOCALAPPDATA "Programs\Rayo"
$exe      = Join-Path $target "rayo.exe"
$shortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Rayo.lnk"

# El exe instalado en marcha esta bloqueado y la copia fallaria a medias. Solo estorba ese:
# el de build\ puede seguir abierto mientras se instala.
$running = Get-Process -Name rayo -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
if ($running) { throw "Cierra Rayo antes de instalar o desinstalar." }

if ($Uninstall) {
    if (Test-Path $shortcut) { Remove-Item $shortcut }
    if (Test-Path $target) { Remove-Item $target -Recurse }
    Write-Host "Desinstalado."
    # La configuracion no se borra: es del usuario, no del programa.
    Write-Host "Tu configuracion y tus marcadores siguen en $env:APPDATA\Rayo."
    return
}

if (-not (Test-Path $Source)) {
    throw "No encuentro $Source. Compila primero con: cmake --build build"
}

New-Item -ItemType Directory -Force $target | Out-Null
Copy-Item $Source $exe -Force

# WScript.Shell es el creador de accesos directos que trae Windows: nada que instalar.
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($shortcut)
$link.TargetPath = $exe          # el icono lo saca del propio exe
$link.WorkingDirectory = $target
$link.Description = "Rayo, gestor de archivos"
$link.Save()

$version = (Get-Item $exe).VersionInfo.ProductVersion
Write-Host "Rayo $version instalado en $target"
Write-Host "Busca 'Rayo' en el menu Inicio; desde ahi se puede anclar."
Write-Host "Para quitarlo: powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall"
