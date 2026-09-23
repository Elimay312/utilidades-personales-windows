# Comprueba que el codigo cumple SEGURIDAD.md. Devuelve 0 si todo esta limpio.
#
# Mira SOLO codigo, no comentarios ni documentacion. Hace falta: el propio SEGURIDAD.md
# nombra todas las APIs prohibidas para explicar por que lo estan, y los fuentes llevan
# comentarios del estilo "el loopback no esta aqui y no va a estar". Un grep a secas se
# encuentra a si mismo y la auditoria nunca sale limpia, con lo que deja de servir de
# puerta.
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
        $cierre = '\*/'
        foreach ($l in Get-Content $ruta) {
            $n++
            $t = $l.Trim()
            if ($bloque) { if ($t -match $cierre) { $bloque = $false }; continue }
            if ($t -match '^/\*') { if ($t -notmatch '\*/') { $bloque = $true; $cierre = '\*/' }; continue }
            # Los comentarios XML del .csproj tambien son comentarios. Sin esto, explicar
            # EN EL CSPROJ por que NO esta una dependencia hace saltar la regla que
            # comprueba que no esta: la auditoria se acusa a si misma. Encontrado en el
            # hud el dia que se quito System.Management, y estaba en los cinco.
            if ($t -match '^<!--') { if ($t -notmatch '-->') { $bloque = $true; $cierre = '-->' }; continue }
            if ($t.StartsWith('//') -or $t.StartsWith('*')) { continue }
            $lineas += [pscustomobject]@{ Fichero = $ruta; Linea = $n; Texto = $l }
        }
    }
    return $lineas
}

$fuentes = @(Get-ChildItem -Filter *.cs -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
$codigo  = Codigo ($fuentes + 'NativeMethods.txt' + 'Isla.csproj')

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

    # La regla que este proyecto necesitaba y el dock no tenia. IAudioClient entra en la
    # lista a proposito: el medidor se activa directo desde IMMDevice, sin pasar por el,
    # asi que si aparece es que alguien esta abriendo un flujo de audio.
    @{ n = '11 grabar audio';                p = 'IAudioCaptureClient|IAudioClient\b|LOOPBACK|WasapiLoopback|waveIn[A-Z]|\beCapture\b' }

    # Proxy de "no se guarda historial": la config se escribe con WriteAllText y nada mas.
    # Un append o una escritura binaria solo tienen sentido para acumular algo.
    @{ n = '12 historial a disco';           p = 'File\.Append|StreamWriter|WriteAllBytes\(' }

    @{ n = '13 notificaciones ajenas';       p = 'UserNotificationListener|UserNotification\b' }
    @{ n = '14 portapapeles y perfil';       p = 'OleGetClipboard|GetClipboardData|OpenClipboard|SetClipboardData|AutomaticDestinations' }

    # ShowWindow y SetWindowPos sobre la ventana PROPIA son necesarios y no estan aqui.
    # Lo que se prohibe es lo que solo tiene sentido sobre ventanas de otros.
    # AllowSetForegroundWindow NO es SetForegroundWindow: no pone a nadie delante, le deja a
    # quien aviso hacerlo una vez (s.3.7). Sin el (?<!Allow) la regla se la confundia.
    # SetForegroundWindow(_hwnd) es la ventana PROPIA, que TrackPopupMenu necesita para que el
    # menu se cierre al clicar fuera (s.3.8). La linea suelta es su entrada en NativeMethods.txt.
    @{ n = '15 tocar ventanas ajenas';       p = 'EnumWindows|EnumChildWindows|PrintWindow|(?<!Allow)SetForegroundWindow(?!\(_hwnd\)|\s*$)|ShowWindowAsync|AttachThreadInput|DWMWA_CLOAK\b' }

    @{ n = '16 matar procesos';              p = 'TerminateProcess|TerminateThread|EndTask|ExitWindowsEx|NtTerminate' }

    # Lanzar algo solo sirve para abrir isla.json con su programa (s.3.8). Cualquier otro
    # Process.Start, o un ShellExecute a mano, es abrir cosas que no son de la isla.
    # (?<!Use) deja pasar UseShellExecute, que es la propiedad de ProcessStartInfo.
    @{ n = '17 lanzar procesos';             p = '(?<!Use)ShellExecute|CreateProcess|WinExec|Process\.Start(?!\(new ProcessStartInfo\(Config\.Ruta\))' }
)

$fallos = 0
Write-Output "Auditoria de SEGURIDAD.md  (isla)"
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
if ($autoarranque) { $donde = 'si, en HKCU\...\Run' } else { $donde = 'no se escribe' }
Write-Output ("  {0,-34} {1}" -f '8  autoarranque visible en HKCU', $donde)

# La cara positiva de la regla 11: si se toca audio, tiene que ser la SALIDA.
$audio = $codigo | Where-Object { $_.Texto -match 'IAudioMeterInformation|GetDefaultAudioEndpoint' }
if ($audio) {
    $render = $codigo | Where-Object { $_.Texto -match '\beRender\b' }
    if ($render) { $dir = 'si, y sobre eRender (salida)' } else { $dir = 'SE USA Y NO SE VE eRender' ; $fallos++ }
} else {
    $dir = 'todavia no se toca audio'
}
Write-Output ("  {0,-34} {1}" -f '11 medidor de pico', $dir)

# El centinela del nombre del dispositivo (SEGURIDAD.md s.3.3, enmienda del 22-09-2026).
# La isla puede preguntarle su nombre al endpoint que YA tiene abierto, que es el
# predeterminado. Lo que no puede es construir una lista: en cuanto aparezca un
# EnumAudioEndpoints o un GetDevice hay inventario de dispositivos, que es justo lo que la
# regla 15 le prohibe a las ventanas. IPolicyConfig va aparte: es la API no documentada que
# CAMBIA el predeterminado, y la isla se entera de los cambios, no los hace.
$inventario = $codigo | Where-Object { $_.Texto -match 'EnumAudioEndpoints|IPolicyConfig|\bGetDevice\(' }
if ($inventario) {
    $fallos++
    Write-Output ("  {0,-34} INCUMPLE (s.3.3)" -f 'sin inventario de dispositivos')
    $inventario | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} else {
    Write-Output ("  {0,-34} {1}" -f 'sin inventario de dispositivos', 'si, solo el predeterminado')
}

# La cara positiva de la enmienda: si se lee el property store, que sea UNA propiedad y
# que sea la del nombre. Cualquier otra clave tendria que justificarse en s.3.3.
$props = $codigo | Where-Object { $_.Texto -match 'PKEY_' }
$otras = $props | Where-Object { $_.Texto -notmatch 'PKEY_Device_FriendlyName' }
if ($otras) {
    $fallos++
    Write-Output ("  {0,-34} INCUMPLE (s.3.3)" -f 'solo el nombre del dispositivo')
    $otras | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} elseif ($props) {
    Write-Output ("  {0,-34} {1}" -f 'solo el nombre del dispositivo', 'si, solo PKEY_Device_FriendlyName')
} else {
    Write-Output ("  {0,-34} {1}" -f 'solo el nombre del dispositivo', 'no se lee ninguna propiedad')
}

# Ceder el primer plano, si, pero solo al PID de quien aviso: nunca a cualquiera.
$cualquiera = $codigo | Where-Object { $_.Texto -match 'ASFW_ANY|AllowSetForegroundWindow\(\s*(uint\.MaxValue|-1|0xFFFFFFFF)' }
if ($cualquiera) {
    $fallos++
    Write-Output ("  {0,-34} INCUMPLE (s.3.7)" -f 'primer plano solo a quien aviso')
    $cualquiera | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
}

# El buzon de avisos (SEGURIDAD.md s.3.7, enmienda del 22-09-2026). Si hay tuberia, tiene que
# ser un servidor y tiene que negarle la entrada a las sesiones de red. Un cliente querria decir que la isla
# llama a alguien, y eso no lo hace nunca.
$tuberia = $codigo | Where-Object { $_.Texto -match 'NamedPipeServerStream|NamedPipeClientStream|CreateNamedPipe|CallNamedPipe' }
if ($tuberia) {
    $cliente = $tuberia | Where-Object { $_.Texto -match 'NamedPipeClientStream|CallNamedPipe|CreateNamedPipe' }
    # .NET no rechaza clientes remotos por su cuenta: lo que lo cierra es denegar el SID NETWORK.
    $soloTuyo = $codigo | Where-Object { $_.Texto -match 'WellKnownSidType\.NetworkSid' }
    if ($cliente -or -not $soloTuyo) {
        $fallos++
        Write-Output ("  {0,-34} INCUMPLE (s.3.7)" -f 'buzon de avisos')
        ($cliente + $tuberia) | Select-Object -Unique Fichero, Linea, Texto | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
    } else {
        Write-Output ("  {0,-34} {1}" -f 'buzon de avisos', 'si, servidor, sin NETWORK')
    }
} else {
    Write-Output ("  {0,-34} {1}" -f 'buzon de avisos', 'no hay tuberia')
}

# El mezclador por app (SEGURIDAD.md s.3.3, enmienda del 23-09-2026). Las sesiones de audio
# de otras apps solo se tocan desde Audio.cs (y se declaran en NativeMethods.txt), y nada
# las vigila en segundo plano ni les toca el silencio o los canales.
$mezclaFuera = $codigo | Where-Object {
    $_.Texto -match 'IAudioSessionManager2|ISimpleAudioVolume|IAudioSessionControl2' -and
    $_.Fichero -notmatch '(^|[\\/])(Audio\.cs|NativeMethods\.txt)$'
}
$mezclaVigila = $codigo | Where-Object { $_.Texto -match 'RegisterAudioSessionNotification|IChannelAudioVolume|SetMute\b' }
if ($mezclaFuera -or $mezclaVigila) {
    $fallos++
    Write-Output ("  {0,-34} INCUMPLE (s.3.3)" -f 'mezclador acotado')
    @($mezclaFuera) + @($mezclaVigila) | Where-Object { $_ } | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} else {
    Write-Output ("  {0,-34} {1}" -f 'mezclador acotado', 'si, solo en Audio.cs')
}

if (Test-Path 'NativeMethods.txt') {
    $pinvokes = (Get-Content NativeMethods.txt | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('//') }).Count
    $lista = "$pinvokes entradas en NativeMethods.txt"
} else {
    $lista = 'todavia no hay NativeMethods.txt'
}
Write-Output ("  {0,-34} {1}" -f 'lista cerrada de P/Invokes', $lista)

Write-Output ("-" * 58)
if ($fallos -eq 0) { Write-Output "TODO LIMPIO"; exit 0 }
Write-Output "$fallos regla(s) incumplida(s)"
exit 1
