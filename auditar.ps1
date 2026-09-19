# Comprueba que el codigo cumple SEGURIDAD.md. Devuelve 0 si todo esta limpio.
#
# Mira SOLO codigo, no comentarios ni documentacion. Hace falta: el propio SEGURIDAD.md
# nombra todas las APIs prohibidas para explicar por que lo estan, y los fuentes llevan
# comentarios del estilo "aqui no hay ningun hook y no lo va a haber". Un grep a secas se
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
$codigo  = Codigo ($fuentes + 'NativeMethods.txt' + 'Lanzador.csproj')

# --- las reglas -------------------------------------------------------------------
# El numero es el articulo de SEGURIDAD.md. Si aqui aparece una regla que no esta alli,
# o al reves, una de las dos esta mal.
$reglas = @(
    @{ n = '1  driver, servicio, elevacion'; p = '\.sys\b|WinRing0|CreateService|schtasks' }

    # Las dos reglas que este proyecto necesitaba mas afiladas que los vecinos, porque
    # aqui SI se reciben teclas. Lo permitido es WM_CHAR/WM_KEYDOWN en la ventana propia
    # y RegisterHotKey; lo prohibido es todo lo que ve el teclado sin tener el foco.
    @{ n = '2  hooks globales';              p = 'SetWindowsHookEx|SetWinEventHook|WH_KEYBOARD|WH_MOUSE' }
    @{ n = '3  teclado fuera de la ventana'; p = 'GetAsyncKeyState|GetKeyboardState|keybd_event|SendInput|GetRawInputData|RegisterRawInputDevices' }

    @{ n = '4  inyeccion de proceso';        p = 'CreateRemoteThread|WriteProcessMemory|VirtualAllocEx|NtMapViewOfSection' }
    @{ n = '5  reemplazo del shell';         p = 'Winlogon|AppInit_DLLs|Image File Execution' }
    @{ n = '6  persistencia oculta';         p = 'SpecialFolder\.Startup|CommonStartup|ScheduledTask' }

    # Uri NO esta aqui a proposito, al reves que en la isla: el lanzador construye URLs
    # para pasarselas al navegador, y ademas usa Uri para validar el esquema (§3.5). Lo
    # que se prohibe es abrir un socket, no nombrar una direccion.
    @{ n = '7  red';                         p = 'HttpClient|WebClient|WebRequest|HttpRequestMessage|\bSocket\b|TcpClient|UdpClient|Dns\.' }

    @{ n = '8  ofuscacion y compresion';     p = '(PublishTrimmed|EnableCompressionInSingleFile|PublishSingleFile)>\s*true' }
    @{ n = '9  codigo en runtime';           p = 'Assembly\.Load|Reflection\.Emit|DynamicMethod|AssemblyLoadContext' }

    # Regla 10: se lanza una ENTRADA del indice, nunca una cadena que hayas escrito. Si
    # aparece un interprete, alguien esta construyendo una shell.
    @{ n = '10 cadena como comando';         p = '\bcmd\.exe\b|\bpowershell\.exe\b|\bwscript\b|\bcscript\b|\bmshta\b|\brundll32\b|/c\s|/k\s' }

    # Proxy de "no se guarda lo que escribes": la config y el uso se escriben con
    # WriteAllText y nada mas. Un append o una escritura binaria solo tienen sentido para
    # acumular algo, y aqui lo unico que se acumula es el contador de lanzamientos.
    @{ n = '11 guardar lo que escribes';     p = 'File\.Append|StreamWriter|WriteAllBytes\(' }

    # Proxy de "solo nombres y rutas, nunca contenido". ReadAllText queda fuera porque es
    # como se leen lanzador.json y uso.json; lo demas solo sirve para mirar dentro de un
    # fichero que no es nuestro.
    @{ n = '12 leer contenido de ficheros';  p = 'ReadAllBytes|ReadAllLines|File\.OpenRead|FileStream\(|StreamReader|MemoryMappedFile' }

    @{ n = '13 navegador y credenciales';    p = 'places\.sqlite|Login Data|CredRead|CredEnumerate|CryptUnprotectData|\\\\User Data\\\\' }
    @{ n = '14 portapapeles';                p = 'OpenClipboard|GetClipboardData|SetClipboardData|OleGetClipboard|Clipboard\.' }

    # SetForegroundWindow NO esta aqui: esta permitido sobre el HWND propio y se
    # comprueba aparte, mas abajo. Es la unica excepcion de la regla 15.
    @{ n = '15 tocar ventanas ajenas';       p = 'EnumWindows|EnumChildWindows|PrintWindow|ShowWindowAsync|AttachThreadInput|DWMWA_CLOAK\b|GetWindowTextW' }

    @{ n = '16 matar procesos';              p = 'TerminateProcess|TerminateThread|EndTask|ExitWindowsEx|NtTerminate|\.Kill\(' }
    @{ n = '17 elevar lo que se lanza';      p = '"runas"|requireAdministrator|highestAvailable' }
)

$fallos = 0
Write-Output "Auditoria de SEGURIDAD.md  (lanzador)"
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

# Regla 1 y 17, por el lado del manifiesto: tiene que pedir asInvoker explicitamente.
if (Test-Path 'app.manifest') {
    $m = Get-Content app.manifest -Raw
    if ($m -match 'level="asInvoker"' -and $m -notmatch 'requireAdministrator|highestAvailable') {
        $nivel = 'asInvoker'
    } else {
        $nivel = 'EL MANIFIESTO NO PIDE asInvoker'; $fallos++
    }
} else {
    $nivel = 'FALTA app.manifest'; $fallos++
}
Write-Output ("  {0,-34} {1}" -f '1  nivel de ejecucion', $nivel)

$autoarranque = $codigo | Where-Object { $_.Texto -match 'CurrentVersion\\\\Run|CurrentVersion\\Run' }
if ($autoarranque) { $donde = 'si, en HKCU\...\Run' } else { $donde = 'no se escribe' }
Write-Output ("  {0,-34} {1}" -f '6  autoarranque visible en HKCU', $donde)

# La excepcion de la regla 15, y la comprobacion que un grep a secas no puede hacer:
# la API esta PERMITIDA, lo que esta prohibido es a quien se le aplica. Tiene que
# aparecer como mucho una vez, en LanzadorWindow.cs, y con el handle propio.
#
# Se cuentan APARICIONES, no lineas: dos llamadas en la misma linea colaban la segunda
# cuando esto contaba lineas. Lo encontro la sonda, no la lectura.
function Veces { param($Lineas, [string]$Patron)
    ($Lineas | ForEach-Object { [regex]::Matches($_.Texto, $Patron).Count } | Measure-Object -Sum).Sum
}

$fg = $codigo | Where-Object { $_.Texto -match 'SetForegroundWindow' }
$fgTotal  = Veces $fg 'SetForegroundWindow'
$fgPropio = Veces $fg 'SetForegroundWindow\(\s*_hwnd\s*\)'
$fgFuera  = $fg | Where-Object { $_.Fichero -ne 'LanzadorWindow.cs' }

if (-not $fg) {
    $estado = 'todavia no se usa'
} elseif ($fgTotal -gt 1) {
    $estado = "APARECE $fgTotal VECES, solo se permite 1"; $fallos++
} elseif ($fgFuera) {
    $estado = "FUERA DE LanzadorWindow.cs ($($fgFuera[0].Fichero))"; $fallos++
} elseif ($fgPropio -ne $fgTotal) {
    $estado = 'NO SE LE PASA EL HANDLE PROPIO (_hwnd)'; $fallos++
} else {
    $estado = 'si, 1 vez y sobre _hwnd'
}
Write-Output ("  {0,-34} {1}" -f '15 SetForegroundWindow', $estado)

# La cara positiva de la regla 10 y de §3.5: si se abren URLs, el esquema se valida
# antes. Sin esa comprobacion, una plantilla del JSON abre lo que quiera.
$urls = $codigo | Where-Object { $_.Texto -match 'UriScheme|Uri\.' }
if ($urls) {
    $valida = $codigo | Where-Object { $_.Texto -match 'UriSchemeHttps|UriSchemeHttp' }
    if ($valida) { $esq = 'si, se comprueba http/https' } else { $esq = 'SE ABREN URLS SIN VALIDAR EL ESQUEMA'; $fallos++ }
} else {
    $esq = 'todavia no se abren URLs'
}
Write-Output ("  {0,-34} {1}" -f '10 esquema de las plantillas', $esq)

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
