<#
    Deja un .NET 10 capaz de compilar y de ejecutar el lanzador, sin administrador.

    Un Windows 11 recien instalado no trae el SDK ni el runtime de escritorio. El runtime
    de Program Files, si algun dia aparece solo, no compila: hace falta el SDK. Y el exe
    publicado es framework-dependent (SEGURIDAD.md regla 8 prohibe el single-file), asi
    que al arrancar busca Microsoft.WindowsDesktop.App. Si vive en el perfil, el host no
    lo ve hasta que DOTNET_ROOT apunta ahi: si no, el autoarranque de HKCU\...\Run falla
    en el siguiente inicio de sesion sin decir por que.

        powershell -NoProfile -ExecutionPolicy Bypass -File preparar.ps1
#>

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_NOLOGO = '1'

function Probar-Sdk10([string]$Cmd) {
    if (-not $Cmd) { return $false }
    if ($Cmd -ne 'dotnet' -and -not (Test-Path $Cmd)) { return $false }
    try {
        $sdks = & $Cmd --list-sdks 2>$null
        return [bool]($sdks -like '*10.*')
    } catch {
        return $false
    }
}

function Tiene-Escritorio10([string]$DotnetExe) {
    $dir = Split-Path -Parent $DotnetExe
    $fx = Join-Path $dir 'shared\Microsoft.WindowsDesktop.App'
    if (-not (Test-Path $fx)) { return $false }
    return [bool](Get-ChildItem $fx -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like '10.*' })
}

function Elegir-Dotnet10 {
    $cmd = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($cmd -and (Probar-Sdk10 $cmd.Source) -and (Tiene-Escritorio10 $cmd.Source)) {
        return $cmd.Source
    }

    $local = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet\dotnet.exe'
    if ((Probar-Sdk10 $local) -and (Tiene-Escritorio10 $local)) {
        return $local
    }

    # El de Program Files puede tener el runtime 10 y no el SDK. No sirve para publicar.
    return $null
}

function Publicar-Entorno([string]$DotnetExe) {
    $dir = Split-Path -Parent $DotnetExe
    $env:DOTNET_ROOT = $dir
    if ($env:PATH -notlike "$($dir)*") { $env:PATH = "$dir;$env:PATH" }

    # Solo se persiste cuando el runtime esta en el perfil. Pisar DOTNET_ROOT del
    # usuario con Program Files esconderia un SDK que ya se instalo ahi a proposito.
    $perfil = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet'
    if ($dir -ne $perfil) { return }

    [Environment]::SetEnvironmentVariable('DOTNET_ROOT', $dir, 'User')
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (-not $userPath) { $userPath = '' }
    if ($userPath -notlike "*$dir*") {
        $nuevo = if ($userPath) { "$dir;$userPath" } else { $dir }
        [Environment]::SetEnvironmentVariable('Path', $nuevo, 'User')
    }
}

Write-Host "Comprobando .NET 10 para el lanzador..."

$dotnetExe = Elegir-Dotnet10

if (-not $dotnetExe) {
    if (-not [Environment]::Is64BitProcess) {
        throw "Hace falta PowerShell de 64 bits: el lanzador es x64."
    }

    Write-Host "No esta el SDK de .NET 10 con el runtime de escritorio. Descargando el instalador oficial de Microsoft..."
    $script = Join-Path $env:TEMP 'dotnet-install.ps1'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $ProgressPreference = 'SilentlyContinue'
    Invoke-WebRequest -Uri 'https://dot.net/v1/dotnet-install.ps1' -OutFile $script -UseBasicParsing

    $destino = Join-Path $env:LOCALAPPDATA 'Microsoft\dotnet'
    Write-Host "Instalando el SDK 10.0 (x64) en $destino ..."
    & $script -Channel 10.0 -Architecture x64 -InstallDir $destino
    if ($LASTEXITCODE -ne 0) { throw "dotnet-install del SDK termino con codigo $LASTEXITCODE." }

    # El SDK suele traer el runtime de escritorio. Si no vino, el exe publicado no arranca:
    # pide Microsoft.WindowsDesktop.App y no Microsoft.NETCore.App.
    $dotnetExe = Join-Path $destino 'dotnet.exe'
    if (-not (Tiene-Escritorio10 $dotnetExe)) {
        Write-Host "El SDK no trajo el runtime de escritorio. Instalando Microsoft.WindowsDesktop.App 10 ..."
        & $script -Channel 10.0 -Runtime windowsdesktop -Architecture x64 -InstallDir $destino
        if ($LASTEXITCODE -ne 0) { throw "dotnet-install del runtime de escritorio termino con codigo $LASTEXITCODE." }
    }

    Remove-Item $script -Force -ErrorAction SilentlyContinue
    $dotnetExe = Elegir-Dotnet10
    if (-not $dotnetExe) { throw "La instalacion de .NET 10 no dejo un SDK utilizable." }
}

Publicar-Entorno $dotnetExe
Write-Host "SDK: $dotnetExe"
& $dotnetExe --version
& $dotnetExe --list-runtimes | Where-Object { $_ -like '*WindowsDesktop.App 10.*' }

if (Test-Path 'Lanzador.csproj') {
    Write-Host "Restaurando NuGet (CsWin32, que es un generador y no viaja en el exe)..."
    & $dotnetExe restore Lanzador.csproj
    if ($LASTEXITCODE -ne 0) { throw "dotnet restore fallo." }
}

Write-Host "Listo. Siguiente paso: powershell -NoProfile -ExecutionPolicy Bypass -File .\instalar.ps1"
