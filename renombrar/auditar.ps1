# Comprueba que el codigo cumple SEGURIDAD.md. Devuelve 0 si todo esta limpio.
#
# Mira SOLO codigo, no comentarios ni documentacion. Hace falta: el propio SEGURIDAD.md
# nombra todas las APIs prohibidas para explicar por que lo estan, y los fuentes llevan
# comentarios del estilo "aqui no se borra nada y no se va a borrar". Un grep a secas se
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
$codigo  = Codigo ($fuentes + 'NativeMethods.txt' + 'Renombrar.csproj')

# --- las reglas -------------------------------------------------------------------
# El numero es el articulo de SEGURIDAD.md. Si aqui aparece una regla que no esta alli,
# o al reves, una de las dos esta mal.
$reglas = @(
    @{ n = '1  driver, servicio, elevacion'; p = '\.sys\b|WinRing0|CreateService|schtasks' }

    # Las dos reglas de este proyecto. Son las que separan "renombra" de "destruye", y
    # por eso van con patrones anchos a proposito: aqui un falso positivo cuesta mucho
    # menos que un falso negativo.
    @{ n = '2  borrar';                      p = 'File\.Delete|Directory\.Delete|\.Delete\(|DeleteFile|SHFileOperation|IFileOperation|RecycleBin|FileIO\.FileSystem' }
    # El tercer argumento posicional de File.Move/File.Copy es el overwrite, y se cuela
    # sin escribir la palabra. Probado metiendo File.Move(a, b, true): con el patron
    # anterior, que solo buscaba "overwrite:", esta regla decia "limpio".
    @{ n = '3  sobrescribir';                p = 'overwrite\s*:\s*true|File\.(Move|Copy)\([^)]*,[^)]*,|WriteAllBytes\(|FileMode\.(Create|Truncate)|MOVEFILE_REPLACE' }

    # Proxy de "solo nombres y fechas, nunca contenido". ReadAllText y WriteAllText quedan
    # fuera porque es como se leen y escriben renombrar.json y ultimo-lote.json; lo demas
    # solo sirve para mirar dentro de un fichero que no es nuestro.
    @{ n = '4  leer contenido';              p = 'ReadAllBytes|ReadAllLines|File\.OpenRead|File\.Open\(|FileStream\(|StreamReader|MemoryMappedFile' }

    @{ n = '5  recorrer el disco';           p = 'AllDirectories|EnumerateDirectories|GetDirectories|GetLogicalDrives|DriveInfo|SHGetKnownFolderPath' }
    @{ n = '8  red';                         p = 'HttpClient|WebClient|WebRequest|HttpRequestMessage|\bSocket\b|TcpClient|UdpClient|Dns\.' }
    @{ n = '9  portapapeles';                p = 'OpenClipboard|GetClipboardData|SetClipboardData|OleGetClipboard|Clipboard\.' }
    @{ n = '10 teclado fuera de la ventana'; p = 'SetWindowsHookEx|SetWinEventHook|WH_KEYBOARD|GetAsyncKeyState|GetKeyboardState|keybd_event|SendInput|RegisterRawInputDevices' }

    # Regla 11: un renombrador no lanza nada. No hay excepcion, asi que el patron puede
    # ser tan ancho como se quiera.
    @{ n = '11 ejecutar algo';               p = 'Process\.Start|ShellExecute|CreateProcess|\bcmd\.exe\b|\bpowershell\.exe\b|\bwscript\b|\bmshta\b|\brundll32\b' }

    @{ n = '12 ofuscacion y compresion';     p = '(PublishTrimmed|EnableCompressionInSingleFile|PublishSingleFile)>\s*true' }
    @{ n = '12 codigo en runtime';           p = 'Assembly\.Load|Reflection\.Emit|DynamicMethod|AssemblyLoadContext' }
    @{ n = '13 registro y persistencia';     p = 'Registry\b|RegCreateKey|RegSetValue|CurrentVersion\\+Run|SpecialFolder\.Startup|CommonStartup' }
    @{ n = '14 ventanas y procesos ajenos';  p = 'EnumWindows|EnumChildWindows|PrintWindow|AttachThreadInput|SetForegroundWindow|CreateRemoteThread|WriteProcessMemory|VirtualAllocEx|TerminateProcess|\.Kill\(' }
    @{ n = '15 atributos, permisos, ACLs';   p = 'SetAttributes|FileAttributes\s*=|SetAccessControl|FileSecurity|icacls|\.Encrypt\(|\.Decrypt\(' }
)

$fallos = 0
Write-Output "Auditoria de SEGURIDAD.md  (renombrar)"
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

# Regla 1, por el lado del manifiesto: tiene que pedir asInvoker explicitamente.
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

# Las dos comprobaciones que un patron prohibido no puede hacer, porque aqui la API esta
# PERMITIDA y lo que se vigila es DONDE y COMO se llama (SEGURIDAD.md §5.2).
#
# Se cuentan APARICIONES, no lineas: la leccion que el lanzador pago en su H0, donde dos
# llamadas en la misma linea colaban la segunda.
function Veces { param($Lineas, [string]$Patron)
    ($Lineas | ForEach-Object { [regex]::Matches($_.Texto, $Patron).Count } | Measure-Object -Sum).Sum
}

$mover = $codigo | Where-Object { $_.Texto -match 'File\.Move' }
$fuera = $mover | Where-Object { $_.Fichero -ne 'Aplicar.cs' }
if (-not $mover) {
    $estado = 'todavia no se mueve nada'
} elseif ($fuera) {
    $estado = "FUERA DE Aplicar.cs ($($fuera[0].Fichero):$($fuera[0].Linea))"; $fallos++
} else {
    # Un File.Move con tres argumentos es un overwrite aunque diga false, y la regla 3 ya
    # lo caza; esto lo dice con el nombre del articulo correcto.
    $conTres = Veces $mover 'File\.Move\([^)]*,[^)]*,'
    if ($conTres -gt 0) { $estado = 'PASA overwrite A File.Move'; $fallos++ }
    else { $estado = "si, $(Veces $mover 'File\.Move') vez/veces y solo en Aplicar.cs" }
}
Write-Output ("  {0,-34} {1}" -f '2/3 File.Move solo en Aplicar', $estado)

# §5.1: el motor y la previa son logica pura. Si escriben, --check deja de comprobar lo
# que se ejecuta de verdad y la previa puede mentir sobre lo que va a pasar.
$puras = $codigo | Where-Object { $_.Fichero -in @('Regla.cs', 'Previa.cs') -and $_.Texto -match 'File\.(Move|Copy|WriteAll|Create)|Directory\.Create' }
if ($puras) {
    $pureza = "Regla/Previa ESCRIBEN ($($puras[0].Fichero):$($puras[0].Linea))"; $fallos++
} else {
    $pureza = 'si, no escriben en el disco'
}
Write-Output ("  {0,-34} {1}" -f '5.1 motor y previa son puros', $pureza)

# §3.5: una expresion regular del usuario sin timeout cuelga la ventana pensando.
$regex = $codigo | Where-Object { $_.Texto -match 'new Regex\(|Regex\.(Replace|Match|IsMatch)' }
if (-not $regex) {
    $tiempo = 'todavia no hay expresiones regulares'
} elseif ($codigo | Where-Object { $_.Texto -match 'TimeSpan\.From' }) {
    $tiempo = 'si, con timeout'
} else {
    $tiempo = 'EXPRESIONES REGULARES SIN TIMEOUT'; $fallos++
}
Write-Output ("  {0,-34} {1}" -f '3.5 regex con timeout', $tiempo)

if (Test-Path 'NativeMethods.txt') {
    $pinvokes = (Get-Content NativeMethods.txt | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith('//') }).Count
    $lista = "$pinvokes entradas en NativeMethods.txt"
} else {
    $lista = 'todavia no hay NativeMethods.txt'; $fallos++
}
Write-Output ("  {0,-34} {1}" -f 'lista cerrada de P/Invokes', $lista)

Write-Output ("-" * 58)
if ($fallos -eq 0) { Write-Output "TODO LIMPIO"; exit 0 }
Write-Output "$fallos regla(s) incumplida(s)"
exit 1
