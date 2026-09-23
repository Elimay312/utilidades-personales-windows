<#
    Pone al dia las utilidades instaladas en este equipo: git pull, y reinstala desde el
    fuente las que cambiaron desde la ultima vez. La red la usan git y los instaladores;
    las apps siguen sin conectarse a nada (la regla de red de cada SEGURIDAD.md).

        powershell -NoProfile -ExecutionPolicy Bypass -File actualizar.ps1
        powershell -NoProfile -ExecutionPolicy Bypass -File actualizar.ps1 -Todo
        powershell -NoProfile -ExecutionPolicy Bypass -File actualizar.ps1 -Solo hud,isla
        powershell -NoProfile -ExecutionPolicy Bypass -File actualizar.ps1 -Programar

    Sin opciones solo toca las que ya estan instaladas aqui. -Todo las instala todas
    (equipo nuevo); -Solo instala o reinstala esas y nada mas. -Programar lo deja
    corriendo solo al iniciar sesion; para quitarlo:
        Unregister-ScheduledTask 'Utilidades - actualizar'

    Lo que falta para compilar se instala solo: el SDK de .NET 10 en el perfil, y las
    Build Tools de C++ con winget (esas si piden administrador).

    El commit instalado de cada una se guarda en %LOCALAPPDATA%\Utilidades\instalado. Si
    una falla no se apunta, y se reintenta la proxima vez. Registro: ..\actualizar.log.
#>
param(
    [switch]$Todo,
    [string[]]$Solo,
    [switch]$Programar
)

$ErrorActionPreference = 'Stop'
$repo   = $PSScriptRoot
$la     = $env:LOCALAPPDATA
$datos  = Join-Path $la 'Utilidades'
$sellos = Join-Path $datos 'instalado'
$tarea  = 'Utilidades - actualizar'

if ($Programar) {
    $accion = New-ScheduledTaskAction -Execute 'powershell.exe' `
        -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Minimized -File `"$PSCommandPath`""
    $cuando = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
    $cuando.Delay = 'PT2M'   # que haya red y escritorio antes de compilar nada
    Register-ScheduledTask $tarea -Action $accion -Trigger $cuando -Force | Out-Null
    Write-Host "Programado al iniciar sesion. Registro: $datos\actualizar.log"
    return
}

New-Item -ItemType Directory -Force $sellos | Out-Null
Start-Transcript -Path (Join-Path $datos 'actualizar.log') | Out-Null

# Cada script de proyecto en su propio proceso: hacen Set-Location, cambian
# $ErrorActionPreference y alguno lanza excepciones; asi un fallo es un codigo de salida.
function Ejecutar([string]$Script, [string[]]$Argumentos) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $Script @Argumentos | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Script salio con codigo $LASTEXITCODE." }
}

# Los instaladores encienden el autoarranque si no se les dice lo contrario: al actualizar
# se respeta lo que ya habia en la config de este equipo.
function SinAutoArranque([string]$Config, [string]$Clave) {
    if ((Test-Path $Config) -and ((Get-Content $Config -Raw) -match "`"$Clave`"\s*:\s*false")) {
        '-SinAutoArranque'
    }
}

function Vivo([string]$Exe) {
    Get-Process ([IO.Path]::GetFileNameWithoutExtension($Exe)) -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $Exe }
}

function Dotnet10 {
    $propio = Join-Path $la 'Microsoft\dotnet\dotnet.exe'
    foreach ($d in 'dotnet', $propio) {
        try { if ((& $d --list-sdks) -like '10.*') { return $d } } catch { }
    }
    Ejecutar (Join-Path $repo 'dock\preparar.ps1')   # SDK al perfil, sin administrador
    $env:DOTNET_ROOT = Split-Path $propio
    return $propio
}

# hud, quicklook y renombrar no tienen instalador: su README es un dotnet publish.
function Publicar([string]$Carpeta, [string]$Exe, [bool]$Arrancar) {
    $dotnet = Dotnet10
    $antes = Vivo $Exe
    if ($antes) { $antes | Stop-Process -Force; Start-Sleep -Milliseconds 400 }
    & $dotnet publish -c Release -o (Split-Path $Exe) (Join-Path $repo $Carpeta) | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'dotnet publish fallo.' }
    if ($antes -or $Arrancar) { Start-Process $Exe }
}

$script:msvc = $false
function Msvc {
    if ($script:msvc) { return }
    # El preparar de Brujula ya sabe encontrar MSVC, CMake y Ninja; si no los hay, se
    # instalan las Build Tools y se vuelve a intentar. El segundo fallo dice que falta.
    $preparar = Join-Path $repo 'proyectos-github\preparar.ps1'
    try { . $preparar -SoloEntorno | Out-Host }
    catch {
        Write-Host 'Faltan las Build Tools de C++. Instalando con winget (pide administrador)...'
        winget install --id Microsoft.VisualStudio.2022.BuildTools --accept-source-agreements `
            --accept-package-agreements --override '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' | Out-Host
        . $preparar -SoloEntorno | Out-Host
    }
    $script:msvc = $true
}

$proyectos = @(
    @{ Nombre = 'dock'; Exe = "$la\Dock\app\Dock.exe"
       Instalar = { param($nuevo) Ejecutar "$repo\dock\instalar.ps1" (SinAutoArranque "$la\Dock\dock.json" 'autoStart') } }
    @{ Nombre = 'isla'; Exe = "$la\Isla\app\Isla.exe"
       Instalar = { param($nuevo) Ejecutar "$repo\isla\instalar.ps1" (SinAutoArranque "$la\Isla\isla.json" 'autoArranque') } }
    @{ Nombre = 'lanzador'; Exe = "$la\Lanzador\app\Lanzador.exe"
       Instalar = { param($nuevo)
           $a = @(SinAutoArranque "$la\Lanzador\lanzador.json" 'autoArranque')
           if (-not $nuevo) { $a += '-SinEverything' }   # Everything se resolvio al instalar
           Ejecutar "$repo\lanzador\instalar.ps1" $a } }
    @{ Nombre = 'hud'; Exe = "$la\Hud\app\Hud.exe"
       Instalar = { param($nuevo) Publicar 'hud' "$la\Hud\app\Hud.exe" $nuevo } }
    @{ Nombre = 'quicklook'; Exe = "$la\QuickLook\app\QuickLook.exe"
       Instalar = { param($nuevo) Publicar 'quicklook' "$la\QuickLook\app\QuickLook.exe" $nuevo } }
    @{ Nombre = 'renombrar'; Exe = "$la\Renombrar\app\Renombrar.exe"
       Instalar = { param($nuevo) Publicar 'renombrar' "$la\Renombrar\app\Renombrar.exe" $false } }
    @{ Nombre = 'calendario'; Exe = "$la\Programs\Agenda\Agenda.exe"
       Instalar = { param($nuevo)
           Msvc
           Ejecutar "$repo\calendario\empaquetar.ps1"
           $antes = Vivo "$la\Programs\Agenda\Agenda.exe"
           $p = Start-Process "$repo\calendario\build\release\Instalar-Agenda.exe" '--silent' -Wait -PassThru
           if ($p.ExitCode -ne 0) { throw "Instalar-Agenda salio con codigo $($p.ExitCode)." }
           if ($antes -or $nuevo) { Start-Process "$la\Programs\Agenda\Agenda.exe" } } }
    @{ Nombre = 'panel-de-control'; Exe = "$la\Programs\Panel\Panel.exe"
       Instalar = { param($nuevo)
           Msvc
           Ejecutar "$repo\panel-de-control\empaquetar.ps1"
           $antes = Vivo "$la\Programs\Panel\Panel.exe"
           $p = Start-Process "$repo\panel-de-control\build\release\Instalar-Panel.exe" '--silent' -Wait -PassThru
           if ($p.ExitCode -ne 0) { throw "Instalar-Panel salio con codigo $($p.ExitCode)." }
           if ($antes -or $nuevo) { Start-Process "$la\Programs\Panel\Panel.exe" } } }
    @{ Nombre = 'proyectos-github'; Exe = "$la\Programs\Brujula\brujula.exe"
       Instalar = { param($nuevo)
           Ejecutar "$repo\proyectos-github\empaquetar.ps1"
           # ponytail: su instalador no tiene modo silencioso y pregunta; darle un --silent
           # como el de Agenda si molesta al iniciar sesion.
           Start-Process "$repo\proyectos-github\build\Instalador-Brujula.exe" -Wait
           $hecho = "$la\Programs\Brujula\brujula.exe"
           if (-not (Test-Path $hecho) -or
               (Get-FileHash $hecho).Hash -ne (Get-FileHash "$repo\proyectos-github\build\brujula.exe").Hash) {
               throw 'El instalador de Brujula se cancelo.'
           } } }
    @{ Nombre = 'rayo-file-manager'; Exe = "$la\Programs\Rayo\rayo.exe"
       Instalar = { param($nuevo)
           Msvc
           $dir = "$repo\rayo-file-manager"
           cmake -S $dir -B "$dir\build" -G Ninja -DCMAKE_BUILD_TYPE=Release | Out-Host
           if ($LASTEXITCODE -ne 0) { throw 'Fallo la configuracion de CMake.' }
           cmake --build "$dir\build" | Out-Host
           if ($LASTEXITCODE -ne 0) { throw 'Fallo la compilacion.' }
           Ejecutar "$dir\install.ps1" } }
)

# Con -File, "-Solo hud,isla" llega como una sola cadena.
$Solo = $Solo -split ','
foreach ($s in $Solo) {
    if ($s -notin $proyectos.Nombre) { Write-Warning "No hay ningun proyecto '$s'." }
}

git -C $repo pull --ff-only | Out-Host
if ($LASTEXITCODE -ne 0) { Write-Warning 'git pull fallo; sigo con lo que hay en local.' }

$fallos = @()
foreach ($p in $proyectos) {
    $n = $p.Nombre
    $commit = git -C $repo log -1 --format=%H -- $n
    $sello = Join-Path $sellos $n
    $instalado = Test-Path $p.Exe

    $pedido = $Todo -or $Solo -contains $n
    if ($Solo -and -not $pedido) { continue }
    if (-not $pedido) {
        if (-not $instalado) { continue }   # no se usa en este equipo
        if ((Test-Path $sello) -and (Get-Content $sello) -eq $commit) {
            Write-Host "${n}: al dia"
            continue
        }
    }

    Write-Host "== $n" -ForegroundColor Cyan
    try {
        & $p.Instalar (-not $instalado)
        Set-Content $sello $commit
        Write-Host "${n}: actualizado" -ForegroundColor Green
    } catch {
        Write-Warning "${n}: $_"
        $fallos += $n
    }
}

Stop-Transcript | Out-Null
if ($fallos) {
    Write-Warning "Fallaron: $($fallos -join ', '). Se reintentan la proxima vez."
    exit 1
}
