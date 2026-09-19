# Comprueba que el codigo cumple SEGURIDAD.md. Devuelve 0 si todo esta limpio.
#
# Mira SOLO codigo, no comentarios ni documentacion. Hace falta: el propio SEGURIDAD.md
# nombra todas las APIs prohibidas para explicar por que lo estan, y los fuentes llevan
# comentarios del estilo "el hook no esta aqui y no va a estar". Un grep a secas se
# encuentra a si mismo y la auditoria nunca sale limpia, con lo que deja de servir de
# puerta.
#
#   pwsh -File auditar.ps1        (o powershell -File, si no hay PowerShell 7)

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

$fuentes = @(Get-ChildItem -Filter *.cs -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
$codigo  = Codigo ($fuentes + 'NativeMethods.txt' + 'Hud.csproj')

# --- las reglas -------------------------------------------------------------------
# El numero es el articulo de SEGURIDAD.md. Si aqui aparece una regla que no esta alli,
# o al reves, una de las dos esta mal.
$reglas = @(
    @{ n = '1  driver, servicio, elevacion'; p = '\.sys\b|WinRing0|CreateService|schtasks|requireAdministrator' }
    @{ n = '2  sensores de hardware';        p = '\b(RDMSR|WRMSR|__inbyte|__outbyte|SMBus)\b' }
    @{ n = '3  hooks globales';              p = 'SetWindowsHookEx|SetWinEventHook' }
    @{ n = '4  leer el teclado';             p = 'GetAsyncKeyState|GetKeyboardState|keybd_event|SendInput|WH_KEYBOARD' }
    @{ n = '5  inyeccion de proceso';        p = 'CreateRemoteThread|WriteProcessMemory|VirtualAllocEx|NtMapViewOfSection' }
    @{ n = '6  reemplazo del shell';         p = 'Winlogon|AppInit_DLLs|Image File Execution' }
    @{ n = '7  red';                         p = 'HttpClient|WebClient|WebRequest|Socket|Dns\.|Uri\(' }
    @{ n = '9  ofuscacion y compresion';     p = '(PublishTrimmed|EnableCompressionInSingleFile|PublishSingleFile)>\s*true' }
    @{ n = '10 codigo en runtime';           p = 'Assembly\.Load|Reflection\.Emit|DynamicMethod' }

    # Heredada de la isla. El HUD necesita UN escalar del mezclador; abrir un flujo de
    # audio no tiene ninguna razon de estar aqui. IAudioClient entra a proposito: el
    # volumen se activa directo desde IMMDevice, sin pasar por el.
    @{ n = '11 grabar audio';                p = 'IAudioCaptureClient|IAudioClient\b|LOOPBACK|WasapiLoopback|waveIn[A-Z]|\beCapture\b' }

    # Proxy de "no se guarda historial": la config se escribe con WriteAllText y nada
    # mas. Un append o una escritura binaria solo tienen sentido para acumular algo.
    @{ n = '12 historial a disco';           p = 'File\.Append|StreamWriter|WriteAllBytes\(' }

    @{ n = '13 notificaciones ajenas';       p = 'UserNotificationListener|UserNotification\b' }
    @{ n = '14 portapapeles y perfil';       p = 'OleGetClipboard|GetClipboardData|OpenClipboard|SetClipboardData|AutomaticDestinations' }

    # SEGURIDAD.md s.1 abrio UNA grieta en esta regla: mover el host del flyout nativo con
    # SetWindowPos. Todo lo demas que solo tiene sentido sobre ventanas de otros sigue
    # aqui, y la grieta tiene su propia puerta mas abajo (regla 17).
    #
    # ShowWindow y SetWindowPos sobre la ventana PROPIA son necesarios y no estan aqui;
    # no se pueden distinguir por regex de los que van sobre la ajena, y por eso el
    # control real de la excepcion es "solo en FlyoutNativo.cs".
    @{ n = '15 tocar ventanas ajenas';       p = 'EnumWindows|EnumChildWindows|PrintWindow|SetForegroundWindow|ShowWindowAsync|AttachThreadInput|DWMWA_CLOAK\b|OpenProcess|GetWindowThreadProcessId' }

    @{ n = '16 matar procesos';              p = 'TerminateProcess|TerminateThread|EndTask|ExitWindowsEx|NtTerminate' }
)

$fallos = 0
Write-Output "Auditoria de SEGURIDAD.md  (hud)"
Write-Output ("-" * 62)

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

# --- la excepcion de s.1, confinada -------------------------------------------------
# La unica operacion permitida sobre una ventana ajena vive en UN fichero. Esta regla es
# la que hace que la excepcion sea auditable: quien quiera saberlo todo sobre ella lee
# FlyoutNativo.cs y ya esta. Si se escapa de ahi, deja de ser una excepcion y pasa a ser
# una costumbre.
$busqueda = $codigo | Where-Object { $_.Texto -match 'FindWindowW?\b|FindWindowExW?\b' }
$fugas    = $busqueda | Where-Object { $_.Fichero -ne 'FlyoutNativo.cs' -and $_.Fichero -ne 'NativeMethods.txt' }
if ($fugas) {
    $fallos++
    Write-Output ("  {0,-34} INCUMPLE" -f '17 buscar ventanas fuera de sitio')
    $fugas | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} else {
    if ($busqueda) { $d = 'confinada a FlyoutNativo.cs' } else { $d = 'todavia no se busca ninguna' }
    Write-Output ("  {0,-34} {1}" -f '17 excepcion del flyout', $d)
}

# --- lo que SI tiene que estar ----------------------------------------------------
Write-Output ("-" * 62)

$autoarranque = $codigo | Where-Object { $_.Texto -match 'CurrentVersion\\\\Run|CurrentVersion\\Run' }
if ($autoarranque) { $donde = 'si, en HKCU\...\Run' } else { $donde = 'no se escribe' }
Write-Output ("  {0,-34} {1}" -f '8  autoarranque visible en HKCU', $donde)

# La cara positiva de la regla 11: si se toca audio, tiene que ser la SALIDA.
$audio = $codigo | Where-Object { $_.Texto -match 'IAudioEndpointVolume|GetDefaultAudioEndpoint' }
if ($audio) {
    $render = $codigo | Where-Object { $_.Texto -match '\beRender\b' }
    if ($render) { $dir = 'si, y sobre eRender (salida)' } else { $dir = 'SE USA Y NO SE VE eRender'; $fallos++ }
} else {
    $dir = 'todavia no se toca audio'
}
Write-Output ("  {0,-34} {1}" -f '11 volumen maestro', $dir)

# La cara positiva de s.3.3: el brillo se LEE. Si aparece WmiSetBrightness, o se ha
# enmendado el documento o alguien se ha pasado de lo acordado.
$escribeBrillo = $codigo | Where-Object { $_.Texto -match 'WmiSetBrightness' }
if ($escribeBrillo) {
    $fallos++
    Write-Output ("  {0,-34} SE ESCRIBE (s.3.3 dice que no)" -f '3.3 brillo de solo lectura')
    $escribeBrillo | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} else {
    Write-Output ("  {0,-34} {1}" -f '3.3 brillo de solo lectura', 'si')
}

if (Test-Path 'NativeMethods.txt') {
    $pinvokes = (Get-Content NativeMethods.txt | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('//') }).Count
    $lista = "$pinvokes entradas en NativeMethods.txt"
} else {
    $lista = 'todavia no hay NativeMethods.txt'
}
Write-Output ("  {0,-34} {1}" -f 'lista cerrada de P/Invokes', $lista)

Write-Output ("-" * 62)
if ($fallos -eq 0) { Write-Output "TODO LIMPIO"; exit 0 }
Write-Output "$fallos regla(s) incumplida(s)"
exit 1
