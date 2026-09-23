<#
    Empaqueta Panel en un único instalador: build\release\Instalar-Panel.exe.

    Compila el preset release (Panel.exe y, con él dentro, el instalador) y dice dónde quedó
    y cuánto pesa.

        powershell -NoProfile -ExecutionPolicy Bypass -File empaquetar.ps1
#>
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# El cmake del PATH en estas máquinas (el de WinLibs) no trae almacén de certificados, y la
# descarga de SQLite muere con un error de SSL que parece de red y no lo es.
if (-not $env:CURL_CA_BUNDLE) {
    $ca = Join-Path $env:ProgramFiles 'Git\usr\ssl\certs\ca-bundle.crt'
    if (Test-Path $ca) { $env:CURL_CA_BUNDLE = $ca }
}

if (-not (Test-Path 'build\release\CMakeCache.txt')) {
    cmake --preset release
    if ($LASTEXITCODE -ne 0) { throw 'Falló la configuración del preset release.' }
}

cmake --build --preset release --target panel_installer
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación del instalador.' }

$instalador = Get-Item 'build\release\Instalar-Panel.exe'
Write-Host ('Instalador listo: {0} ({1:N1} MB)' -f $instalador.FullName, ($instalador.Length / 1MB))
