<#
    Instalacion por usuario: copia el exe a %LOCALAPPDATA%\Programs\Rayo, deja un acceso
    directo en el menu Inicio y anade "Abrir en Rayo" al menu contextual de carpetas y
    unidades. Todo bajo HKCU: no pide administrador.

        powershell -ExecutionPolicy Bypass -File install.ps1
        powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
#>
param(
    [string]$Source = "$PSScriptRoot\build\rayo.exe",
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"

# El Explorador cachea los verbos del menu contextual: sin este aviso, "Abrir en Rayo" no
# aparece (ni desaparece al desinstalar) hasta reiniciarlo.
Add-Type -Namespace Rayo -Name Shell -MemberDefinition @"
[DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, IntPtr a, IntPtr b);
"@

$target   = Join-Path $env:LOCALAPPDATA "Programs\Rayo"
$exe      = Join-Path $target "rayo.exe"
$shortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\Rayo.lnk"

# "Abrir en Rayo". Tres sitios porque el Explorador los trata como cosas distintas: la
# carpeta que tienes seleccionada, el fondo de la carpeta que tienes abierta (ahi la ruta
# es %V y no %1) y la raiz de una unidad, que no entra en "Directory".
$verbs = @(
    @{ Key = "HKCU:\Software\Classes\Directory\shell\Rayo";            Arg = '"%1"' },
    @{ Key = "HKCU:\Software\Classes\Directory\Background\shell\Rayo"; Arg = '"%V"' },
    @{ Key = "HKCU:\Software\Classes\Drive\shell\Rayo";                Arg = '"%1"' }
)

# El exe instalado en marcha esta bloqueado y la copia fallaria a medias. Solo estorba ese:
# el de build\ puede seguir abierto mientras se instala.
$running = Get-Process -Name rayo -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
if ($running) { throw "Cierra Rayo antes de instalar o desinstalar." }

if ($Uninstall) {
    foreach ($v in $verbs) { Remove-Item $v.Key -Recurse -ErrorAction SilentlyContinue }
    if (Test-Path $shortcut) { Remove-Item $shortcut }
    if (Test-Path $target) { Remove-Item $target -Recurse }
    [Rayo.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)  # SHCNE_ASSOCCHANGED
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

foreach ($v in $verbs) {
    New-Item -Path "$($v.Key)\command" -Force | Out-Null
    Set-ItemProperty -Path $v.Key -Name "(Default)" -Value "Abrir en Rayo"
    Set-ItemProperty -Path $v.Key -Name "Icon" -Value "$exe,0"
    Set-ItemProperty -Path "$($v.Key)\command" -Name "(Default)" -Value "`"$exe`" $($v.Arg)"
}

[Rayo.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)  # SHCNE_ASSOCCHANGED

$version = (Get-Item $exe).VersionInfo.ProductVersion
Write-Host "Rayo $version instalado en $target"
Write-Host "Busca 'Rayo' en el menu Inicio; desde ahi se puede anclar."
# En Windows 11 el menu corto solo admite verbos de apps empaquetadas (MSIX +
# IExplorerCommand): este sale en el clasico, que es 'Mostrar mas opciones' o Shift+F10.
Write-Host "Menu contextual: 'Abrir en Rayo' en carpetas y unidades (en Windows 11, dentro"
Write-Host "de 'Mostrar mas opciones' o con Shift+F10)."
Write-Host "Para quitarlo: powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall"
