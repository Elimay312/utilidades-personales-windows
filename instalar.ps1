<#
    Instalacion por usuario: publica el exe a %LOCALAPPDATA%\Isla\app, deja un acceso
    directo en el menu Inicio y enciende el autoarranque. Todo bajo HKCU: no pide
    administrador y no toca la carpeta Inicio ni el programador de tareas
    (SEGURIDAD.md 3.6, el autoarranque es HKCU\...\Run y nada mas).

        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1
        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -SinAutoArranque
        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -Desinstalar

    Hace falta el SDK de .NET 10 en la maquina de destino. Se publica desde el fuente a
    proposito: SEGURIDAD.md 9 prohibe empaquetar, comprimir y recortar el binario, asi
    que no hay un zip que copiar.
#>
param(
    [switch]$SinAutoArranque,
    [switch]$Desinstalar
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$raiz    = Join-Path $env:LOCALAPPDATA 'Isla'
$destino = Join-Path $raiz 'app'
$exe     = Join-Path $destino 'Isla.exe'
$config  = Join-Path $raiz 'isla.json'
$acceso  = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Isla.lnk'
$run     = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'

# Solo estorba la copia INSTALADA: el exe de bin\ puede seguir abierto mientras se
# instala. Si se deja viva, Copy-Item falla a medias y queda una instalacion rota.
function Parar {
    $p = Get-Process Isla -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
    if (-not $p) { return }
    $p | Stop-Process -Force
    Start-Sleep -Milliseconds 400
}

if ($Desinstalar) {
    Parar
    # El valor de Run lo borraria la propia isla al arrancar con autoArranque en false,
    # pero desinstalada ya no arranca nunca: hay que quitarlo aqui o queda una entrada
    # muerta en la pestana Inicio del Administrador de tareas.
    Remove-ItemProperty -Path $run -Name 'Isla' -ErrorAction SilentlyContinue
    if (Test-Path $acceso)  { Remove-Item $acceso }
    if (Test-Path $destino) { Remove-Item $destino -Recurse -Force }
    Write-Host "Desinstalada."
    # La configuracion no se borra: es del usuario, no del programa.
    Write-Host "Tu isla.json sigue en $raiz."
    return
}

Parar

dotnet publish -c Release -o $destino
if ($LASTEXITCODE -ne 0) { throw "dotnet publish fallo. Hace falta el SDK de .NET 10." }

# La isla siembra sola su isla.json en el primer arranque, pero aqui hace falta antes:
# el autoarranque se enciende EN EL JSON, no en el registro.
if (-not (Test-Path $config)) {
    New-Item -ItemType Directory -Force $raiz | Out-Null
    Copy-Item (Join-Path $destino 'isla.json') $config
}

if (-not $SinAutoArranque) {
    # Se toca el JSON y no HKCU directamente porque la isla reescribe el valor de Run en
    # cada arranque segun lo que diga esto: una entrada puesta a mano duraria hasta el
    # siguiente inicio de sesion y despues desapareceria sola.
    $antes   = Get-Content $config -Raw
    $despues = $antes -replace '("autoArranque"\s*:\s*)false', '${1}true'
    if ($despues -ne $antes) {
        Set-Content $config $despues -Encoding UTF8 -NoNewline
    }
    elseif ($antes -notmatch '"autoArranque"\s*:\s*true') {
        Write-Warning "No encuentro autoArranque en $config; ponlo a true a mano."
    }
}

# WScript.Shell es el creador de accesos directos que trae Windows: nada que instalar.
$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($acceso)
$link.TargetPath = $exe          # el icono lo saca del propio exe
$link.WorkingDirectory = $destino
$link.Description = 'Isla dinamica'
$link.Save()

Start-Process $exe

Write-Host "Isla instalada en $destino y arrancada."
Write-Host "Configuracion: $config (se recarga sola al guardarla)."
if ($SinAutoArranque) {
    Write-Host "Sin autoarranque. Para encenderlo: pon autoArranque en true en el json."
} else {
    Write-Host "Autoarranque encendido (pestana Inicio del Administrador de tareas)."
}
Write-Host "Para quitarla: powershell -ExecutionPolicy Bypass -File instalar.ps1 -Desinstalar"
