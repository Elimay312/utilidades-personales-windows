<#
    Instalacion por usuario. Publica en %LOCALAPPDATA%\Lanzador\app, deja un acceso
    directo en el menu Inicio y enciende el autoarranque en el JSON: el propio lanzador
    escribe HKCU\...\Run al arrancar, y nada mas (SEGURIDAD.md 3.9).

    En un Windows 11 sin nada mas, descarga lo que falta:
      - el SDK de .NET 10 y el runtime de escritorio, al perfil, sin administrador
      - Everything 1.4 x64 de voidtools, si no esta. Su servicio si pide administrador:
        indexar NTFS sin el exige correr elevado, y este programa no va a hacerlo.

    La red esta aqui, no en el exe. Regla 7 y el parrafo de 3.7.

        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1
        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -SinAutoArranque
        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -SinEverything
        powershell -NoProfile -ExecutionPolicy Bypass -File instalar.ps1 -Desinstalar

    Cargar las funciones sin instalar (para probar la busqueda de Everything):
        . .\instalar.ps1
#>
param(
    [switch]$SinAutoArranque,
    [switch]$SinEverything,
    [switch]$Desinstalar
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'

# Fijado a la estable que listaba voidtools.com/downloads el 2026-09-21.
# La Lite no vale: la propia documentacion dice que no permite IPC, y sin el buzon
# el lanzador no ve ficheros. Al subir de version se cambian las tres cosas juntas.
$EverythingVersion = '1.4.1.1032'
$EverythingSetup = "Everything-$EverythingVersion.x64-Setup.exe"
$EverythingUrl = "https://www.voidtools.com/$EverythingSetup"
$EverythingSha256 = 'c42efad041d4c0bb4d4ac97ae7cbe89f153ec1fe078772392e749c7f5d5282d3'

$raiz    = Join-Path $env:LOCALAPPDATA 'Lanzador'
$destino = Join-Path $raiz 'app'
$exe     = Join-Path $destino 'Lanzador.exe'
$config  = Join-Path $raiz 'lanzador.json'
$acceso  = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\Lanzador.lnk'
$run     = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'

function Probar-Sdk10([string]$Cmd) {
    if (-not $Cmd) { return $false }
    if ($Cmd -ne 'dotnet' -and -not (Test-Path $Cmd)) { return $false }
    try {
        return [bool]((& $Cmd --list-sdks 2>$null) -like '*10.*')
    } catch {
        return $false
    }
}

function Resolver-Dotnet {
    $cmd = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($cmd -and (Probar-Sdk10 $cmd.Source)) { return $cmd.Source }
    $local = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'
    if (Probar-Sdk10 $local) { return $local }
    return $null
}

# Solo estorba la copia instalada. Un Lanzador.exe de bin\ puede seguir abierto.
function Parar-Instalado {
    $procesos = Get-Process Lanzador -ErrorAction SilentlyContinue
    foreach ($p in $procesos) {
        $ruta = $null
        try { $ruta = $p.Path } catch { }
        if ($ruta -eq $exe) { $p | Stop-Process -Force }
    }
    Start-Sleep -Milliseconds 400
}

function Buscar-Everything {
    $enMarcha = Get-Process Everything -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($enMarcha) {
        try {
            if ($enMarcha.Path -and (Test-Path $enMarcha.Path)) { return $enMarcha.Path }
        } catch { }
    }

    $fijos = @(
        (Join-Path $env:ProgramFiles 'Everything\Everything.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Everything\Everything.exe')
    )
    foreach ($c in $fijos) {
        if ($c -and (Test-Path $c)) { return $c }
    }

    $claves = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
    )
    foreach ($clave in $claves) {
        $hits = Get-ItemProperty $clave -ErrorAction SilentlyContinue |
            Where-Object { $_.DisplayName -eq 'Everything' }
        foreach ($h in $hits) {
            if ($h.InstallLocation) {
                $candidato = Join-Path ($h.InstallLocation.TrimEnd('\')) 'Everything.exe'
                if (Test-Path $candidato) { return $candidato }
            }
        }
    }
    return $null
}

function Recibir-Everything {
    $setup = Join-Path $env:TEMP $EverythingSetup
    $sumaUrl = "https://www.voidtools.com/Everything-$EverythingVersion.sha256"
    Write-Host "Descargando $EverythingSetup ..."
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri $EverythingUrl -OutFile $setup -UseBasicParsing
    # En Windows PowerShell 5.1, que es el que hay en un Windows recien instalado,
    # Content llega como byte[] si el servidor no manda charset. En 7 llega string.
    $resp = Invoke-WebRequest -Uri $sumaUrl -UseBasicParsing
    if ($resp.Content -is [byte[]]) {
        $publicada = [Text.Encoding]::UTF8.GetString($resp.Content)
    } else {
        $publicada = [string]$resp.Content
    }
    $linea = ($publicada -split '\r?\n') | Where-Object { $_ -like "*$EverythingSetup" } | Select-Object -First 1
    if (-not $linea) { throw "El fichero de sumas de voidtools no menciona $EverythingSetup." }
    $anunciado = ($linea -split '\s+')[0].ToLowerInvariant()
    if ($anunciado -ne $EverythingSha256) {
        Remove-Item $setup -Force -ErrorAction SilentlyContinue
        throw "La suma publicada ($anunciado) no es la que tiene fijada este script. No se ejecuta."
    }
    $real = (Get-FileHash -Algorithm SHA256 -Path $setup).Hash.ToLowerInvariant()
    if ($real -ne $EverythingSha256) {
        Remove-Item $setup -Force -ErrorAction SilentlyContinue
        throw "SHA256 del instalador no coincide ($real). No se ejecuta."
    }
    Unblock-File -Path $setup
    return $setup
}

function Asegurar-Everything {
    $ya = Buscar-Everything
    if ($ya) {
        Write-Host "Everything ya esta: $ya"
        $vivo = Get-Process Everything -ErrorAction SilentlyContinue
        if (-not $vivo) {
            # El buzon lo publica la aplicacion, no el servicio. Si no esta abierta,
            # el lanzador dice que no hay ficheros aunque el indice exista.
            Start-Process -FilePath $ya -ArgumentList '-startup'
            Write-Host "Everything no estaba abierto: arrancado en segundo plano."
        }
        return
    }

    Write-Host "Everything no esta. Hace falta para buscar ficheros."
    Write-Host "Windows pedira permiso de administrador una vez: el servicio indexa el disco, y sin el Everything solo funciona elevado."
    try {
        $setup = Recibir-Everything
    } catch {
        Write-Warning $_.Exception.Message
        Write-Warning "Sin Everything el lanzador abre aplicaciones igual."
        return
    }
    try {
        $proc = Start-Process -FilePath $setup -ArgumentList '/S' -Verb RunAs -Wait -PassThru
    } catch {
        Write-Warning "No se instalo Everything ($($_.Exception.Message)). El lanzador abre aplicaciones igual."
        return
    } finally {
        Remove-Item $setup -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $proc.ExitCode -and $proc.ExitCode -ne 0) {
        Write-Warning "El instalador de Everything termino con codigo $($proc.ExitCode)."
    }

    $instalado = $null
    for ($i = 0; $i -lt 20 -and -not $instalado; $i++) {
        $instalado = Buscar-Everything
        if (-not $instalado) { Start-Sleep -Milliseconds 500 }
    }
    if (-not $instalado) {
        Write-Warning "No encuentro Everything.exe despues del instalador. Sin el no hay busqueda de ficheros."
        return
    }

    # El autoarranque de Everything es suyo (HKCU, el que el pone con este flag), no el
    # nuestro. Sin la aplicacion abierta al iniciar sesion, el servicio mantiene el
    # indice y el lanzador no tiene a quien preguntarle.
    & $instalado -install-current-user-run-on-system-startup | Out-Null
    Start-Process -FilePath $instalado -ArgumentList '-startup'
    $servicio = Get-Service -Name 'Everything' -ErrorAction SilentlyContinue
    if ($servicio) {
        Write-Host "Everything instalado, con servicio ($($servicio.Status)) y arranque de sesion."
    } else {
        Write-Warning "Everything quedo instalado pero no veo su servicio. Indexar NTFS va a pedir administrador."
    }
}

function Poner-AutoArranque([bool]$Encender) {
    if (-not (Test-Path $config)) { return }
    $antes = [IO.File]::ReadAllText($config)
    if ($Encender) {
        $despues = $antes -replace '("autoArranque"\s*:\s*)false', '${1}true'
    } else {
        $despues = $antes -replace '("autoArranque"\s*:\s*)true', '${1}false'
        Remove-ItemProperty -Path $run -Name 'Lanzador' -ErrorAction SilentlyContinue
    }
    if ($despues -eq $antes -and $antes -notmatch '"autoArranque"') {
        Write-Warning "No encuentro autoArranque en $config; ponlo a mano."
        return
    }
    if ($despues -ne $antes) {
        [IO.File]::WriteAllText($config, $despues, (New-Object System.Text.UTF8Encoding $false))
    }
}

# Dot-source: deja las funciones y no instala. El .cmd y un powershell -File no entran aqui.
if ($MyInvocation.InvocationName -eq '.') { return }

if (-not [Environment]::Is64BitProcess) {
    throw "Hace falta PowerShell de 64 bits: el lanzador es x64."
}

$build = 0
try { $build = [int](Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').CurrentBuild } catch { }
if ($build -and $build -lt 26100) {
    Write-Warning "Este lanzador pide Windows 11 build 26100 o mas (hay $build)."
}

if ($Desinstalar) {
    Parar-Instalado
    Remove-ItemProperty -Path $run -Name 'Lanzador' -ErrorAction SilentlyContinue
    if (Test-Path $acceso)  { Remove-Item $acceso -Force }
    if (Test-Path $destino) { Remove-Item $destino -Recurse -Force }
    Write-Host "Lanzador desinstalado."
    Write-Host "Tu configuracion sigue en $raiz (lanzador.json y uso.json)."
    Write-Host "Everything, si se instalo, no se toca."
    return
}

Parar-Instalado

$dotnet = Resolver-Dotnet
$perfil = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'
$escritorio = $false
if ($dotnet) {
    $fx = Join-Path (Split-Path -Parent $dotnet) 'shared\Microsoft.WindowsDesktop.App'
    $escritorio = [bool](Get-ChildItem $fx -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -like '10.*' })
}
if (-not $dotnet -or -not $escritorio) {
    Write-Host "Falta .NET 10 o el runtime de escritorio. Preparando el perfil..."
    & (Join-Path $PSScriptRoot 'preparar.ps1')
    $dotnet = Resolver-Dotnet
}
if (-not $dotnet) { throw "No hay un SDK de .NET 10 con el que publicar." }

$dirDotnet = Split-Path -Parent $dotnet
$env:DOTNET_ROOT = $dirDotnet
if ($env:PATH -notlike "$($dirDotnet)*") { $env:PATH = "$dirDotnet;$env:PATH" }

Write-Host "Publicando en $destino ..."
& $dotnet publish -c Release -o $destino Lanzador.csproj
if ($LASTEXITCODE -ne 0) { throw "dotnet publish fallo." }

if (-not (Test-Path $config)) {
    New-Item -ItemType Directory -Force $raiz | Out-Null
    $semilla = Join-Path $destino 'lanzador.json'
    if (-not (Test-Path $semilla)) { $semilla = Join-Path $PSScriptRoot 'lanzador.json' }
    Copy-Item $semilla $config
}

Poner-AutoArranque (-not $SinAutoArranque)

$shell = New-Object -ComObject WScript.Shell
$link = $shell.CreateShortcut($acceso)
$link.TargetPath = $exe
$link.WorkingDirectory = $destino
$link.Description = 'Lanzador: Alt+Espacio'
$link.Save()

if (-not $SinEverything) { Asegurar-Everything }

# --check no pide el mutex ni abre ventana: si el runtime no se ve, falla aqui y no
# despues, en el inicio de sesion.
& $exe --check
if ($LASTEXITCODE -ne 0) { throw "El lanzador publicado no paso --check (codigo $LASTEXITCODE). Suele ser el runtime de escritorio." }

Start-Process $exe
Write-Host "Lanzador instalado en $destino y arrancado."
Write-Host "Configuracion: $config"
if ($SinAutoArranque) {
    Write-Host "Sin autoarranque. Para encenderlo: autoArranque en true, y reinicia el lanzador."
} else {
    Write-Host "Autoarranque encendido (pestana Inicio del Administrador de tareas)."
}
Write-Host "Atajo: Alt+Espacio."
Write-Host "Para quitarlo: powershell -NoProfile -ExecutionPolicy Bypass -File .\instalar.ps1 -Desinstalar"
