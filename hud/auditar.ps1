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
        $cierre = '\*/'
        foreach ($l in Get-Content $ruta) {
            $n++
            $t = $l.Trim()
            if ($bloque) { if ($t -match $cierre) { $bloque = $false }; continue }
            if ($t -match '^/\*') { if ($t -notmatch '\*/') { $bloque = $true; $cierre = '\*/' }; continue }
            # Los comentarios XML del .csproj tambien son comentarios. Sin esto, explicar
            # en el csproj por que NO esta una dependencia hace saltar la regla que
            # comprueba que no esta -- le paso a este mismo fichero el dia que se quito
            # System.Management. El auditar.ps1 de los vecinos tiene el mismo agujero.
            if ($t -match '^<!--') { if ($t -notmatch '-->') { $bloque = $true; $cierre = '-->' }; continue }
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

    # ABSOLUTA. Hubo una excepcion durante unas horas -- apartar el host del aviso nativo
    # con SetWindowPos -- y se retiro al medirla: el aviso no es una ventana, y ademas no
    # hacia falta porque RegisterHotKey ya suprime el aviso de volumen. SEGURIDAD.md s.1.
    #
    # FindWindow entra aqui a proposito y es el centinela que importa: es el primer paso
    # de cualquier intento de volver a abrir esa grieta. El HUD no conoce la existencia
    # de ninguna ventana que no sea la suya.
    #
    # ShowWindow y SetWindowPos sobre la ventana PROPIA son necesarios y no estan aqui;
    # sin FindWindow ni EnumWindows no hay forma de obtener un HWND ajeno al que
    # aplicarlos, asi que la regla se sostiene sola.
    @{ n = '15 tocar ventanas ajenas';       p = 'FindWindowW?\b|FindWindowExW?\b|EnumWindows|EnumChildWindows|PrintWindow|SetForegroundWindow|ShowWindowAsync|AttachThreadInput|DWMWA_CLOAK\b|OpenProcess|GetWindowThreadProcessId' }

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

# Guardia de ALCANCE, no de seguridad, y esta aqui a proposito. El brillo se cayo del
# proyecto midiendo (SEGURIDAD.md s.1): sus teclas van por ACPI, no se pueden capturar, y
# el aviso de Windows saldria igual. Un alcance que no se comprueba se vuelve a ensanchar
# solo, y con el volveria System.Management y todo WMI.
$brillo = $codigo | Where-Object { $_.Texto -match 'WmiMonitor|WmiSetBrightness|System\.Management|ManagementObject|ManagementEventWatcher|root\+WMI' }
if ($brillo) {
    $fallos++
    Write-Output ("  {0,-34} FUERA DE ALCANCE (s.1 y s.4)" -f 'alcance: solo volumen')
    $brillo | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} else {
    Write-Output ("  {0,-34} {1}" -f 'alcance: solo volumen', 'si, nada de brillo ni WMI')
}

# El centinela que va con el aviso de cambio de dispositivo (SEGURIDAD.md s.3.2). El HUD
# sabe QUE ha cambiado el predeterminado, nunca CUAL: al aviso no se le pregunta el id, se
# vuelve a pedir el predeterminado por GetDefaultAudioEndpoint. En cuanto aparezca un
# EnumAudioEndpoints o un GetId hay un inventario de dispositivos de audio, que es lo mismo
# que la regla 15 le prohibe a las ventanas. IPolicyConfig entra aqui aparte: es la API no
# documentada que CAMBIA el predeterminado, y el HUD se entera de los cambios, no los hace.
$inventario = $codigo | Where-Object { $_.Texto -match 'EnumAudioEndpoints|GetId\(|IPolicyConfig|GetDevice\(' }
if ($inventario) {
    $fallos++
    Write-Output ("  {0,-34} INCUMPLE (s.3.2)" -f 'sin inventario de dispositivos')
    $inventario | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} else {
    Write-Output ("  {0,-34} {1}" -f 'sin inventario de dispositivos', 'si, no se lee ningun id')
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
