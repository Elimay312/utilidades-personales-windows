# Comprueba que el codigo cumple SEGURIDAD.md. Devuelve 0 si todo esta limpio.
#
# Mira SOLO codigo, no comentarios ni documentacion. Hace falta: el propio SEGURIDAD.md
# nombra todas las APIs prohibidas para explicar por que lo estan, y los fuentes llevan
# comentarios del estilo "SendInput no esta aqui y no va a estar". Un grep a secas se
# encuentra a si mismo, nunca sale limpio, y deja de servir de puerta.
#
# La diferencia con el auditar.ps1 del dock: alli WH_KEYBOARD_LL esta prohibido y basta
# con buscarlo. Aqui esta permitido, asi que el script no comprueba que NO este: comprueba
# que este DONDE TIENE QUE ESTAR y que no haga mas de lo que dice el §3.1.
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

# Recursivo: el codigo de contenido vive en Content\. Se excluye obj\ y bin\, que
# contienen lo que genera CsWin32 y no es codigo nuestro.
$fuentes = @(
    Get-ChildItem -Recurse -Filter *.cs |
        Where-Object { $_.FullName -notmatch '\\(obj|bin)\\' } |
        ForEach-Object { Resolve-Path -Relative $_.FullName }
)
$codigo = Codigo ($fuentes + 'NativeMethods.txt' + 'QuickLook.csproj')

# --- las reglas -------------------------------------------------------------------
$reglas = @(
    @{ n = '1  driver, servicio, elevacion'; p = '\.sys\b|WinRing0|CreateService|schtasks|requireAdministrator' }
    @{ n = '2  sensores de hardware';        p = '\b(RDMSR|WRMSR|__inbyte|__outbyte|SMBus)\b' }
    @{ n = '3  los otros hooks';             p = 'SetWinEventHook|WH_CBT|WH_GETMESSAGE|WH_SHELL|WH_CALLWNDPROC|WH_JOURNAL|WH_MOUSE_LL|WH_KEYBOARD\b' }
    @{ n = '3  leer el teclado por su cuenta'; p = 'GetAsyncKeyState|GetKeyboardState|ToUnicode|MapVirtualKey' }
    @{ n = '4  inyeccion de proceso';        p = 'CreateRemoteThread|WriteProcessMemory|VirtualAllocEx|NtMapViewOfSection' }
    @{ n = '5  reemplazo del shell';         p = 'Winlogon|AppInit_DLLs|Image File Execution' }
    @{ n = '6  red';                         p = 'HttpClient|WebClient|WebRequest|Socket|Dns\.|Uri\(' }
    @{ n = '8  ofuscacion y compresion';     p = '(PublishTrimmed|EnableCompressionInSingleFile|PublishSingleFile)>\s*true' }
    @{ n = '9  codigo en runtime';           p = 'Assembly\.Load|Reflection\.Emit|DynamicMethod' }
    @{ n = '11 portapapeles';                p = 'OleGetClipboard|GetClipboardData|OpenClipboard|SetClipboardData' }
    @{ n = '12 sintetizar entrada';          p = 'SendInput|keybd_event|mouse_event|INPUT_KEYBOARD' }
    @{ n = '13 matar procesos';              p = 'TerminateProcess|TerminateThread|EndTask|ExitWindowsEx|NtTerminate' }
    @{ n = '13 gobernar ventanas ajenas';    p = 'SetForegroundWindow|AttachThreadInput|DWMWA_CLOAK\b|ABM_SETSTATE' }
    @{ n = 'A  escribir archivos del usuario'; p = 'File\.(Delete|Move|Copy|WriteAll|AppendAll|Create)\b|FileMode\.(Create|Append|Truncate)|OpenWrite|IPersistFile.*Save' }
    @{ n = 'A  cachear previsualizaciones';  p = 'GetTempPath|GetTempFileName|Path\.GetTempFile' }
    @{ n = 'A  enumerar la vista entera';    p = 'SVGIO_ALLVIEW' }
)

$fallos = 0
Write-Output "Auditoria de SEGURIDAD.md"
Write-Output ("-" * 62)

foreach ($r in $reglas) {
    $hits = $codigo | Where-Object { $_.Texto -match $r.p }
    if ($hits) {
        $fallos++
        Write-Output ("  {0,-38} INCUMPLE" -f $r.n)
        $hits | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
    } else {
        Write-Output ("  {0,-38} limpio" -f $r.n)
    }
}

# --- el hook: no que NO este, sino que este acotado --------------------------------
# Esto es lo que sustituye a la regla 3 del dock. Ver SEGURIDAD.md §3.1.
Write-Output ("-" * 62)

$hookHits = $codigo | Where-Object { $_.Texto -match 'WH_KEYBOARD_LL|SetWindowsHookEx' }
$fuera = $hookHits | Where-Object { $_.Fichero -notmatch '(^|\\|/)(Hook\.cs|NativeMethods\.txt)$' }
if ($fuera) {
    $fallos++
    Write-Output ("  {0,-38} INCUMPLE" -f '3.1 el hook solo vive en Hook.cs')
    $fuera | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} else {
    Write-Output ("  {0,-38} si, {1} referencia(s)" -f '3.1 el hook solo vive en Hook.cs', $hookHits.Count)
}

# Dentro de Hook.cs, la unica tecla observable es el espacio. Los tres modificadores
# estan permitidos por el §3.1 para poder dejar pasar Ctrl+Espacio y companeros.
$permitidas = 'VK_SPACE|VK_CONTROL|VK_MENU|VK_SHIFT|VK_LSHIFT|VK_RSHIFT|VK_LCONTROL|VK_RCONTROL|VK_LMENU|VK_RMENU'
$hookCodigo = Codigo @('Hook.cs')
$otras = $hookCodigo |
    ForEach-Object { $l = $_; ([regex]::Matches($l.Texto, 'VK_[A-Z0-9_]+')) | ForEach-Object { [pscustomobject]@{ Linea = $l.Linea; Tecla = $_.Value } } } |
    Where-Object { $_.Tecla -notmatch "^($permitidas)$" }
if ($otras) {
    $fallos++
    Write-Output ("  {0,-38} INCUMPLE" -f '3.1 una sola tecla observada')
    $otras | ForEach-Object { Write-Output ("      Hook.cs:{0}  {1}" -f $_.Linea, $_.Tecla) }
} else {
    Write-Output ("  {0,-38} solo VK_SPACE" -f '3.1 una sola tecla observada')
}

# La seleccion del Explorador se pregunta desde UN solo sitio: HostWindow, que es quien
# abre el panel y quien lleva su temporizador. Si aparece en cualquier otro fichero, alguien
# abrio un camino nuevo a los datos del usuario. Ver SEGURIDAD.md §3.2.
$selHits = $codigo | Where-Object { $_.Texto -match 'Selection\.Path' }
$selFuera = $selHits | Where-Object { $_.Fichero -notmatch '(^|\\|/)HostWindow\.cs$' }
if ($selFuera) {
    $fallos++
    Write-Output ("  {0,-38} INCUMPLE" -f '3.2 la seleccion solo desde HostWindow')
    $selFuera | ForEach-Object { Write-Output ("      {0}:{1}  {2}" -f $_.Fichero, $_.Linea, $_.Texto.Trim()) }
} else {
    Write-Output ("  {0,-38} si, {1} llamada(s)" -f '3.2 la seleccion solo desde HostWindow', $selHits.Count)
}

# Sin memoria: el callback no acumula nada. Si Hook.cs crece mucho, algo se colo.
if (Test-Path 'Hook.cs') {
    $n = $hookCodigo.Count
    $estado = if ($n -le 90) { "$n lineas de codigo" } else { "$n lineas: REVISAR, el §5 dice que se queda pequeno" }
    Write-Output ("  {0,-38} {1}" -f '3.1 Hook.cs legible de una sentada', $estado)
}

# --- lo que SI tiene que estar ----------------------------------------------------
Write-Output ("-" * 62)
$autoarranque = $codigo | Where-Object { $_.Texto -match 'CurrentVersion\\\\Run|CurrentVersion\\Run' }
Write-Output ("  {0,-38} {1}" -f '7  autoarranque visible en HKCU', $(if ($autoarranque) { 'si, en HKCU\...\Run' } else { 'no se escribe' }))

$desengancha = $codigo | Where-Object { $_.Texto -match 'UnhookWindowsHookEx' }
Write-Output ("  {0,-38} {1}" -f '3.1 el hook se desinstala al salir', $(if ($desengancha) { 'si, UnhookWindowsHookEx' } else { 'NO - pendiente' }))

if (Test-Path 'NativeMethods.txt') {
    $pinvokes = (Get-Content NativeMethods.txt | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('//') }).Count
    Write-Output ("  {0,-38} {1}" -f 'lista cerrada de P/Invokes', "$pinvokes entradas en NativeMethods.txt")
}

Write-Output ("-" * 62)
if ($fallos -eq 0) { Write-Output "TODO LIMPIO"; exit 0 }
Write-Output "$fallos regla(s) incumplida(s)"
exit 1
