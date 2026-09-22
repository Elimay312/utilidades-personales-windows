<#
    Comprueba las reglas de SEGURIDAD.md contra el codigo. Devuelve 0 si todo esta limpio
    y 1 si alguna regla se incumple.

        powershell -NoProfile -ExecutionPolicy Bypass -File auditar.ps1

    Mira el CODIGO, no los comentarios, y por eso lo primero que hace es quitarlos: este
    repositorio tiene un SEGURIDAD.md entero y comentarios que lo citan, todos llenos de
    las palabras que las reglas prohiben. Un auditor que no los quitara se denunciaria a
    si mismo en cada ejecucion y acabaria ignorandose.

    Ninguno de los hermanos sabe hacer esto: los suyos limpian comentarios de C# y de XML.
    Este limpia C++ (barra doble y bloques) y CMake (almohadilla).
#>
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# --- Quitar comentarios ------------------------------------------------------------

# Las cadenas se reconocen ANTES que los comentarios y se conservan enteras.
#
# Sin eso, el limpiador se comia la barra doble de una URL y con ella el resto de la
# linea, asi que las reglas 3 y 4 —las que vigilan que no se hable con nadie que no sea
# GitHub— no podian saltar nunca. Lo encontro la sonda de violaciones, no la lectura del
# codigo: el auditor daba TODO LIMPIO con una direccion prohibida delante.
$script:PatronCpp = '"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\r\n]*'
$script:PatronCMake = '"(?:\\.|[^"\\])*"|#[^\r\n]*'

$script:Conservar = [System.Text.RegularExpressions.MatchEvaluator] {
    param($m)
    # Una cadena se queda entera; un comentario se va.
    if ($m.Value.StartsWith('"')) { return $m.Value }
    return ' '
}

function Sin-Comentarios([string]$Texto, [string]$Lenguaje) {
    $patron = if ($Lenguaje -eq 'cpp') { $script:PatronCpp } else { $script:PatronCMake }
    return [regex]::Replace($Texto, $patron, $script:Conservar)
}

function Leer-Codigo {
    $trozos = @()
    foreach ($archivo in Get-ChildItem -Path 'src', 'tests' -Recurse -Include *.cpp, *.h -EA SilentlyContinue) {
        $trozos += [pscustomobject]@{
            Ruta  = $archivo.FullName.Substring($PSScriptRoot.Length + 1)
            Texto = Sin-Comentarios (Get-Content $archivo.FullName -Raw) 'cpp'
        }
    }
    foreach ($archivo in Get-ChildItem -Path . -Filter CMakeLists.txt) {
        $trozos += [pscustomobject]@{
            Ruta  = $archivo.Name
            Texto = Sin-Comentarios (Get-Content $archivo.FullName -Raw) 'cmake'
        }
    }
    return $trozos
}

$codigo = Leer-Codigo
$cmake = (Get-Content (Join-Path $PSScriptRoot 'CMakeLists.txt') -Raw)
$manifiesto = Get-Content (Join-Path $PSScriptRoot 'src\brujula.manifest') -Raw

$resultados = @()

function Regla([string]$Nombre, [string]$Estado, [string]$Detalle = '') {
    $script:resultados += [pscustomobject]@{ Nombre = $Nombre; Estado = $Estado; Detalle = $Detalle }
}

# Devuelve las coincidencias de un patron en todo el codigo, ya sin comentarios.
function Buscar([string]$Patron) {
    $hits = @()
    foreach ($t in $codigo) {
        foreach ($m in [regex]::Matches($t.Texto, $Patron)) {
            $hits += "$($t.Ruta): $($m.Value.Trim())"
        }
    }
    # La coma no sobra: PowerShell desenvuelve un array de un solo elemento, y
    # entonces $hits[0] devuelve la primera LETRA de la unica coincidencia en vez
    # de la coincidencia. El detalle de la tabla salia siempre como (s).
    return ,$hits
}

# --- Las reglas --------------------------------------------------------------------

# 1. Ningun token escrito a mano en el codigo.
$hits = Buscar 'gh[pousr]_[A-Za-z0-9]{16,}|github_pat_[A-Za-z0-9_]{20,}'
if ($hits) { Regla '1. Sin tokens en el codigo' 'FALLA' $hits[0] }
else { Regla '1. Sin tokens en el codigo' 'bien' }

# 2. El token no viaja a ninguna funcion de escritura ni de log.
$hits = Buscar '(?i)\b(printf|wprintf|OutputDebugString\w*|WriteFile|fputs|fwrite|ofstream|Log\w*)\s*\([^;\r\n]*token'
if ($hits) { Regla '2. El token no se registra' 'FALLA' $hits[0] }
else { Regla '2. El token no se registra' 'bien' }

# 3. Todo cifrado. Un literal sin cifrar es un descuido, no una opcion. Los espacios de
#    nombres XML no son peticiones y no cuentan.
$hits = Buscar '"http://(?!www\.w3\.org|schemas\.)'
if ($hits) { Regla '3. Sin http sin cifrar' 'FALLA' $hits[0] }
else { Regla '3. Sin http sin cifrar' 'bien' }

# 4. Un unico destino. Cualquier host que no sea de GitHub tiene que pasar por aqui.
$hosts = Buscar '(?i)"(?:https?://)?([a-z0-9][a-z0-9.-]*\.(?:com|net|org|io|dev|co))'
$ajenos = @($hosts | Where-Object { $_ -notmatch 'github\.com|githubusercontent\.com|sqlite\.org|w3\.org|microsoft\.com' })
if ($ajenos) { Regla '4. Solo se habla con GitHub' 'FALLA' $ajenos[0] }
else { Regla '4. Solo se habla con GitHub' 'bien' }

# 5. Sin telemetria ni comprobacion de actualizaciones.
$hits = Buscar '(?i)\b(telemetry|telemetria|analytics|analitica|appinsights|sentry|crashpad|amplitude|mixpanel)\b'
if ($hits) { Regla '5. Sin telemetria' 'FALLA' $hits[0] }
else { Regla '5. Sin telemetria' 'bien' }

# 6. SQLite no carga extensiones: un archivo de base de datos no puede traer un dll dentro.
if ($cmake -match 'SQLITE_OMIT_LOAD_EXTENSION\s*=\s*1') { Regla '6. SQLite sin extensiones' 'bien' }
else { Regla '6. SQLite sin extensiones' 'FALLA' 'falta SQLITE_OMIT_LOAD_EXTENSION en CMakeLists.txt' }

# 7. Nada de lanzar procesos con cadenas construidas.
$hits = Buscar '\b(system|_wsystem|popen|_wpopen|WinExec)\s*\('
if ($hits) { Regla '7. Sin ejecutar procesos' 'FALLA' $hits[0] }
else { Regla '7. Sin ejecutar procesos' 'bien' }

# 8. Sin privilegios de administrador.
if ($manifiesto -match 'requireAdministrator|highestAvailable') {
    Regla '8. Sin pedir administrador' 'FALLA' 'el manifiesto pide elevacion'
} else { Regla '8. Sin pedir administrador' 'bien' }

# 9. Dependencias fijadas: ninguna apunta a una rama.
$declaraciones = [regex]::Matches($cmake, 'FetchContent_Declare\s*\([^)]*\)')
$sueltas = @()
foreach ($d in $declaraciones) {
    $texto = $d.Value
    $nombre = ([regex]::Match($texto, 'FetchContent_Declare\s*\(\s*(\S+)')).Groups[1].Value
    $fijada = ($texto -match 'GIT_TAG\s+v?[0-9]') -or ($texto -match 'URL_HASH\s+SHA256=')
    if (-not $fijada) { $sueltas += $nombre }
    if ($texto -match 'GIT_TAG\s+(main|master|HEAD|develop)\b') { $sueltas += $nombre }
}
if ($sueltas) { Regla '9. Dependencias fijadas' 'FALLA' ($sueltas -join ', ') }
elseif ($declaraciones.Count -eq 0) { Regla '9. Dependencias fijadas' 'FALLA' 'no encuentro ningun FetchContent_Declare' }
else { Regla '9. Dependencias fijadas' 'bien' }

# 10. La caja fuerte del token. Pendiente hasta la fase 3: la regla existe, el codigo que
#     vigilaria todavia no. Marcarla "bien" ahora seria mentir en la tabla.
$usaCred = Buscar '\bCredWriteW?\s*\('
$guardaFuera = Buscar '(?i)(token[^;\r\n]{0,40}(ofstream|WriteFile|WritePrivateProfile|RegSetValue))'
if ($guardaFuera) { Regla '10. El token solo en la caja fuerte' 'FALLA' $guardaFuera[0] }
elseif ($usaCred) { Regla '10. El token solo en la caja fuerte' 'bien' }
else { Regla '10. El token solo en la caja fuerte' 'pendiente' 'todavia no hay codigo de token (fase 3)' }

# 11. Lo unico que Brujula escribe en un repositorio es PROYECTO.md (SEGURIDAD.md, regla 5).
#     Hasta la fase 5 esta regla no tenia codigo que vigilar; ahora si. Se mira por tres
#     sitios, porque con uno solo se esquiva sin querer:
#
#       a) el nombre del archivo esta en UNA constante y vale exactamente PROYECTO.md;
#       b) toda ruta que empiece por /repos/ termina en esa constante dentro de la misma
#          sentencia — o sea, no hay una segunda ruta a la API de contenidos;
#       c) el unico verbo que llega al cliente REST es PUT. Un DELETE o un PATCH que se
#          colara no daria ningun error aqui: lo daria en el repositorio de alguien.
$nombre = Buscar 'kProyectoFile\s*=\s*L"PROYECTO\.md"'
$rutas = Buscar '"/repos/'
# Vale de las dos maneras: la ruta montada en el codigo —que termina en la constante— y la
# escrita entera, que es como la comprueba la prueba de query_test.cpp.
$rutasBuenas = Buscar '"/repos/"[^;]*kProyectoFile|"/repos/[^"]*PROYECTO\.md"'
$verbos = Buscar 'Rest\(\s*L"[A-Z]+"'
$verbosMalos = @($verbos | Where-Object { $_ -notmatch 'L"PUT"' })

if (-not $nombre) {
    Regla '11. Solo se escribe PROYECTO.md' 'FALLA' 'no encuentro la constante del archivo'
} elseif ($verbosMalos) {
    Regla '11. Solo se escribe PROYECTO.md' 'FALLA' $verbosMalos[0]
} elseif ($rutas.Count -ne $rutasBuenas.Count) {
    Regla '11. Solo se escribe PROYECTO.md' 'FALLA' 'hay una ruta /repos/ que no acaba en PROYECTO.md'
} elseif (-not $rutas) {
    Regla '11. Solo se escribe PROYECTO.md' 'pendiente' 'todavia no hay codigo que escriba'
} else {
    Regla '11. Solo se escribe PROYECTO.md' 'bien'
}

# 12. Lo que entra por brujula:// y por WM_COPYDATA pasa SIEMPRE por la lista blanca, y el
#     registro solo se toca dentro de nuestra propia clave.
#
#     La regla existe desde la fase 8 porque esa fase abrio la primera puerta que no
#     controla el usuario: un esquema propio lo dispara cualquier pagina web, y el
#     WM_COPYDATA lo manda cualquiera que sepa el nombre de nuestra clase de ventana. Se
#     mira por tres sitios:
#
#       a) los dos caminos de entrada —RepoFromUrl y OpenRepoByName— llaman a
#          LooksLikeRepoName. Validar solo en uno deja el otro abierto, y el otro es el que
#          no viene de nuestro propio main;
#       b) toda escritura en el registro va bajo Software\Classes\brujula. Cualquier otra
#          subclave seria la aplicacion tocando cosas del sistema que no son suyas;
#       c) no se escribe en HKLM ni en HKEY_LOCAL_MACHINE, que ademas pediria elevacion y
#          chocaria con la regla 8.
$filtra = Buscar 'LooksLikeRepoName'
$enUrl = Buscar 'LooksLikeRepoName\(name\)\s*\?'
$enMensaje = Buscar 'if\s*\(!LooksLikeRepoName'
$claves = Buscar 'RegCreateKeyExW?\s*\(\s*HKEY_[A-Z_]+'
$clavesMalas = @($claves | Where-Object { $_ -notmatch 'HKEY_CURRENT_USER' })
$rutasRegistro = Buscar 'L"Software\\\\Classes[^"]*"'
$rutasMalas = @($rutasRegistro | Where-Object { $_ -notmatch 'Classes\\\\brujula' })

if (-not $filtra) {
    Regla '12. Lo de fuera pasa por la lista blanca' 'FALLA' 'no encuentro LooksLikeRepoName'
} elseif (-not $enUrl -or -not $enMensaje) {
    Regla '12. Lo de fuera pasa por la lista blanca' 'FALLA' 'uno de los dos caminos de entrada no valida'
} elseif ($clavesMalas) {
    Regla '12. Lo de fuera pasa por la lista blanca' 'FALLA' $clavesMalas[0]
} elseif ($rutasMalas) {
    Regla '12. Lo de fuera pasa por la lista blanca' 'FALLA' $rutasMalas[0]
} else {
    Regla '12. Lo de fuera pasa por la lista blanca' 'bien'
}

# --- Salida ------------------------------------------------------------------------

Write-Output ''
Write-Output 'Auditoria de Brujula'
Write-Output ('-' * 64)
foreach ($r in $resultados) {
    $linea = '{0,-38} {1}' -f $r.Nombre, $r.Estado
    if ($r.Detalle) { $linea += "  ($($r.Detalle))" }
    Write-Output $linea
}
Write-Output ('-' * 64)

$fallos = @($resultados | Where-Object Estado -eq 'FALLA').Count
$pendientes = @($resultados | Where-Object Estado -eq 'pendiente').Count

if ($fallos -eq 0) {
    if ($pendientes -gt 0) { Write-Output "TODO LIMPIO ($pendientes regla(s) pendiente(s) de tener codigo que vigilar)" }
    else { Write-Output 'TODO LIMPIO' }
    exit 0
}
Write-Output "$fallos regla(s) incumplida(s)"
exit 1
