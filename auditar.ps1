# Comprueba que el codigo cumple SEGURIDAD.md. Devuelve 0 si todo esta limpio.
#
# Mira SOLO codigo, no comentarios ni documentacion. Hace falta: el propio
# SEGURIDAD.md nombra todas las APIs prohibidas para explicar por que lo estan, y los
# fuentes llevan comentarios del estilo "ABM_SETSTATE no esta aqui y no va a estar".
# Un grep a secas se encuentra a si mismo y la auditoria nunca sale limpia, con lo que
# deja de servir de puerta.
#
#   pwsh -File auditar.ps1

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# --- lineas de codigo, sin comentarios -------------------------------------------
function Codigo {
    param([string[]]$Rutas)

    $lineas = @()
    foreach ($ruta in $Rutas) {
        if (-not (Test-Path $ruta)) { continue }
        $n = 0
        $bloque = $false
        foreach ($l in Get-Content $ruta) {
            $n++
            $t = $l.Trim()
            if ($bloque) { if ($t -match '\*/') { $bloque = $false }; continue }
            if ($t -match '^/\*') { if ($t -notmatch '\*/') { $bloque = $true }; continue }
            if ($t.StartsWith('//') -or $t.StartsWith('*')) { continue }
            $lineas += [pscustomobject]@{ Fichero = $ruta; Linea = $n; Texto = $l }
        }
    }
    return $lineas
}

$fuentes = @(Get-ChildItem -Filter *.cs | ForEach-Object { $_.Name })
$codigo  = Codigo ($fuentes + 'NativeMethods.txt' + 'Dock.csproj')

# --- las reglas -------------------------------------------------------------------
$reglas = @(
    @{ n = '1  driver, servicio, elevacion'; p = '\.sys\b|WinRing0|CreateService|schtasks|requireAdministrator' }
    @{ n = '2  sensores de hardware';        p = '\b(RDMSR|WRMSR|__inbyte|__outbyte|SMBus)\b' }
    @{ n = '3  hooks globales';              p = 'SetWindowsHookEx|SetWinEventHook' }
    @{ n = '3  leer el teclado';             p = 'GetAsyncKeyState|GetKeyboardState|keybd_event|SendInput|WH_KEYBOARD' }
    @{ n = '4  inyeccion de proceso';        p = 'CreateRemoteThread|WriteProcessMemory|VirtualAllocEx|NtMapViewOfSection' }
    @{ n = '5  reemplazo del shell';         p = 'Winlogon|AppInit_DLLs|Image File Execution' }
    @{ n = '6  red';                         p = 'HttpClient|WebClient|WebRequest|Socket|Dns\.|Uri\(' }
    @{ n = '8  ofuscacion y compresion';     p = '(PublishTrimmed|EnableCompressionInSingleFile|PublishSingleFile)>\s*true' }
    @{ n = '9  codigo en runtime';           p = 'Assembly\.Load|Reflection\.Emit|DynamicMethod' }
    @{ n = '10 portapapeles';                p = 'OleGetClipboard|GetClipboardData|OpenClipboard|SetClipboardData' }
    @{ n = '10 ficheros internos del perfil'; p = 'AutomaticDestinations|CustomDestinations' }
    @{ n = '10 datos de cuenta de Steam';  p = 'loginusers|localconfig|config\.vdf|ssfn|Steam..userdata' }
    @{ n = 'A  matar procesos';              p = 'TerminateProcess|TerminateThread|EndTask|ExitWindowsEx|NtTerminate' }
    @{ n = 'A  cloak sobre ventanas ajenas'; p = 'DWMWA_CLOAK\b' }
    @{ n = 'A  ajuste global de la barra';   p = 'ABM_SETSTATE' }
    @{ n = 'A  captura con borde quitado';   p = 'IsBorderRequired|GraphicsCapture' }
    @{ n = 'A  escribir accesos directos';   p = 'IPersistFile.*Save|IShellLink.*Save' }
    @{ n = 'A  relajar UIPI, secuestrar';    p = 'ChangeWindowMessageFilter|MakeDefault' }
)

$fallos = 0
Write-Output "Auditoria de SEGURIDAD.md"
Write-Output ("-" * 58)

foreach ($r in $reglas) {
    $hits = $codigo | Where-Object { $_.Texto -match $r.p }
    if ($hits) {
        $fallos++
        Write-Output ("  {0,-34} INCUMPLE" -f $r.n)
        $hits | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
    } else {
        Write-Output ("  {0,-34} limpio" -f $r.n)
    }
}

# --- lo que SI tiene que estar ----------------------------------------------------
Write-Output ("-" * 58)
$autoarranque = $codigo | Where-Object { $_.Texto -match 'CurrentVersion\\\\Run|CurrentVersion\\Run' }
Write-Output ("  {0,-34} {1}" -f '7  autoarranque visible en HKCU', $(if ($autoarranque) { 'si, en HKCU\...\Run' } else { 'no se escribe' }))

$pinvokes = (Get-Content NativeMethods.txt | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('//') }).Count
Write-Output ("  {0,-34} {1}" -f 'lista cerrada de P/Invokes', "$pinvokes entradas en NativeMethods.txt")

Write-Output ("-" * 58)
if ($fallos -eq 0) { Write-Output "TODO LIMPIO"; exit 0 }
Write-Output "$fallos regla(s) incumplida(s)"
exit 1
