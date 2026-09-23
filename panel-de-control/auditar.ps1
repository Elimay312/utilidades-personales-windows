<#
    Comprueba las reglas de SEGURIDAD.md contra el codigo. Devuelve 0 si todo esta limpio
    y 1 si alguna regla se incumple.

        powershell -NoProfile -ExecutionPolicy Bypass -File auditar.ps1

    Mira el CODIGO, no los comentarios: los quita primero, conservando las cadenas, igual
    que el de Brujula (proyectos-github/auditar.ps1), del que sale el limpiador.

    Casi todas las reglas son prohibiciones, y una prohibicion se cumple aunque todavia no
    haya codigo. Las que exigen que algo EXISTA (el manifiesto, las dependencias fijadas,
    la copia de la luz nocturna) salen "pendiente" hasta que haya codigo que vigilar.

    Solo ASCII en este archivo: PowerShell 5.1 lee sin BOM como ANSI.
#>
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

# --- Quitar comentarios ------------------------------------------------------------

$script:PatronCpp = '"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\r\n]*'
$script:PatronCMake = '"(?:\\.|[^"\\])*"|#[^\r\n]*'

$script:Conservar = [System.Text.RegularExpressions.MatchEvaluator] {
    param($m)
    if ($m.Value.StartsWith('"')) { return $m.Value }
    return ' '
}

function Sin-Comentarios([string]$Texto, [string]$Lenguaje) {
    $patron = if ($Lenguaje -eq 'cpp') { $script:PatronCpp } else { $script:PatronCMake }
    return [regex]::Replace($Texto, $patron, $script:Conservar)
}

function Leer-Codigo {
    $trozos = @()
    foreach ($dir in 'src', 'tests') {
        if (-not (Test-Path $dir)) { continue }
        foreach ($archivo in Get-ChildItem -Path $dir -Recurse -Include *.cpp, *.h) {
            $texto = Get-Content $archivo.FullName -Raw -Encoding UTF8
            if (-not $texto) { $texto = '' }
            $trozos += [pscustomobject]@{
                Ruta  = $archivo.FullName.Substring($PSScriptRoot.Length + 1)
                Texto = Sin-Comentarios $texto 'cpp'
            }
        }
    }
    return $trozos
}

$codigo = Leer-Codigo

$rutaCMake = Join-Path $PSScriptRoot 'CMakeLists.txt'
$cmake = if (Test-Path $rutaCMake) { Sin-Comentarios (Get-Content $rutaCMake -Raw) 'cmake' } else { $null }

$rutaManifiesto = Join-Path $PSScriptRoot 'assets\panel.manifest'
$manifiesto = if (Test-Path $rutaManifiesto) { Get-Content $rutaManifiesto -Raw } else { $null }

$resultados = @()

function Regla([string]$Nombre, [string]$Estado, [string]$Detalle = '') {
    $script:resultados += [pscustomobject]@{ Nombre = $Nombre; Estado = $Estado; Detalle = $Detalle }
}

# Coincidencias de un patron en el codigo ya sin comentarios, como "ruta: texto".
# -Solo limita la busqueda a las rutas que casan con ese patron; -Menos las excluye.
function Buscar([string]$Patron, [string]$Solo = '', [string]$Menos = '') {
    $hits = @()
    foreach ($t in $codigo) {
        if ($Solo -and $t.Ruta -notmatch $Solo) { continue }
        if ($Menos -and $t.Ruta -match $Menos) { continue }
        foreach ($m in [regex]::Matches($t.Texto, $Patron)) {
            $hits += "$($t.Ruta): $($m.Value.Trim())"
        }
    }
    # La coma evita que PowerShell desenvuelva un array de un elemento (ver Brujula).
    return ,$hits
}

# Sentencias completas (hasta el punto y coma) que contienen un patron. Sirve para las
# reglas que miran que dos cosas vayan JUNTAS en la misma llamada.
function Sentencias([string]$Patron, [string]$Solo = '') {
    return Buscar "[^;{}]*(?:$Patron)[^;{}]*;" $Solo
}

function Prohibido([string]$Nombre, [string]$Patron, [string]$Menos = '') {
    $hits = Buscar $Patron '' $Menos
    if ($hits) { Regla $Nombre 'FALLA' $hits[0] } else { Regla $Nombre 'bien' }
}

# --- Las reglas (numeradas como en SEGURIDAD.md) -----------------------------------

# 1.1 Sin red y sin telemetria. NetworkInformation es una consulta local y no cuenta.
Prohibido '1.1 Sin red' '\b(WinHttp\w*|InternetOpen\w*|InternetConnect\w*|URLDownload\w*|WSAStartup|WSASocket\w*|socket|connect)\s*\(|"https?://|Windows::Web::Http|Windows\.Web\.Http'
Prohibido '1.1 Sin telemetria' '(?i)\b(telemetry|telemetria|analytics|analitica|appinsights|sentry|crashpad|amplitude|mixpanel)\b'

# 1.2 Nada que pida la ubicacion. El SSID sale de GetConnectedSsid, no de WlanAPI.
Prohibido '1.2 Sin pedir la ubicacion' '\bWlan(QueryInterface|GetAvailableNetworkList|GetNetworkBssList|Scan)\b|(?i)geolocat'

# 1.3 Sin administrador.
if ($null -eq $manifiesto) {
    Regla '1.3 Sin pedir administrador' 'pendiente' 'todavia no hay assets\panel.manifest (fase 1)'
} elseif ($manifiesto -match 'requireAdministrator|highestAvailable') {
    Regla '1.3 Sin pedir administrador' 'FALLA' 'el manifiesto pide elevacion'
} elseif ($manifiesto -notmatch 'asInvoker') {
    Regla '1.3 Sin pedir administrador' 'FALLA' 'el manifiesto no dice asInvoker'
} else { Regla '1.3 Sin pedir administrador' 'bien' }

# 1.4 Sin ganchos de teclado ni Raw Input.
Prohibido '1.4 Sin escuchar el teclado' '\b(SetWindowsHookEx\w*|RegisterRawInputDevices)\s*\('

# 1.5 Cerrar es pedir, nunca matar.
Prohibido '1.5 Sin matar procesos' '\bTerminateProcess\s*\(|\bRmForceShutdown\b|\bRmShutdown\s*\([^;]*\b1\b'

# 1.6 Sin ejecutar texto; ShellExecute solo con ms-settings: o el propio panel.json, y
#     CreateProcess solo en system/apps.cpp.
$hits = Buscar '\b(system|_wsystem|popen|_wpopen|WinExec)\s*\('
$shell = @(Sentencias '\bShellExecute\w*\s*\(' | Where-Object { $_ -notmatch 'L"ms-settings:|ConfigPath\s*\(' })
$crear = Buscar '\bCreateProcess\w*\s*\(' '' '^src\\system\\apps\.cpp$'
if ($hits) { Regla '1.6 Sin ejecutar texto' 'FALLA' $hits[0] }
elseif ($shell) { Regla '1.6 Sin ejecutar texto' 'FALLA' "ShellExecute sin ms-settings: ni ConfigPath: $($shell[0])" }
elseif ($crear) { Regla '1.6 Sin ejecutar texto' 'FALLA' "CreateProcess fuera de apps.cpp: $($crear[0])" }
else { Regla '1.6 Sin ejecutar texto' 'bien' }

# 1.7 El registro solo se escribe desde el autoarranque, la luz nocturna y el instalador,
#     y nunca en HKLM.
$escrituras = Buscar '\b(Reg(SetValueEx|SetKeyValue|DeleteValue|DeleteKey|DeleteKeyEx|DeleteTree|CreateKeyEx)\w*|SHSetValue\w*|SHDeleteKey\w*|SHDeleteValue\w*)\s*\(' '' '^src\\(core\\autostart\.h|system\\nightlight\.cpp|installer\\)'
$hklm = Buscar '\bHKEY_LOCAL_MACHINE\b|\bHKLM\b'
if ($escrituras) { Regla '1.7 Registro solo en dos sitios' 'FALLA' $escrituras[0] }
elseif ($hklm) { Regla '1.7 Registro solo en dos sitios' 'FALLA' $hklm[0] }
else { Regla '1.7 Registro solo en dos sitios' 'bien' }

# 1.8 Dependencias fijadas por hash, sin vcpkg.
if (Test-Path (Join-Path $PSScriptRoot 'vcpkg.json')) {
    Regla '1.8 Dependencias fijadas' 'FALLA' 'hay un vcpkg.json: las dependencias van por FetchContent'
} elseif ($null -eq $cmake) {
    Regla '1.8 Dependencias fijadas' 'pendiente' 'todavia no hay CMakeLists.txt (fase 1)'
} else {
    $declaraciones = [regex]::Matches($cmake, 'FetchContent_Declare\s*\([^)]*\)')
    $sueltas = @()
    foreach ($d in $declaraciones) {
        $nombre = ([regex]::Match($d.Value, 'FetchContent_Declare\s*\(\s*(\S+)')).Groups[1].Value
        if ($d.Value -notmatch 'URL_HASH\s+SHA256=[0-9A-Fa-f]{64}') { $sueltas += $nombre }
    }
    if ($sueltas) { Regla '1.8 Dependencias fijadas' 'FALLA' ("sin URL_HASH SHA256: " + ($sueltas -join ', ')) }
    else { Regla '1.8 Dependencias fijadas' 'bien' }
}

# 2.2 IPolicyConfig vive en un solo archivo, y la ruta por aplicacion no se usa.
$fuera = Buscar '\bIPolicyConfig\w*|(?i)870af99c|f8679f50' '' '^src\\system\\policy_config\.h$'
$porApp = Buscar '\bIAudioPolicyConfigFactory\b'
if ($fuera) { Regla '2.2 IPolicyConfig en un solo archivo' 'FALLA' $fuera[0] }
elseif ($porApp) { Regla '2.2 IPolicyConfig en un solo archivo' 'FALLA' $porApp[0] }
else { Regla '2.2 IPolicyConfig en un solo archivo' 'bien' }

# 2.3 WMI: solo ROOT\WMI y solo las clases WmiMonitorBrightness*.
$espacios = @(Buscar '(?i)"root\\\\\w+' | Where-Object { $_ -notmatch '(?i)"root\\\\wmi$' })
$win32 = Buscar '(?i)\bWin32_\w+|\bMSFT_\w+|\bCIM_\w+'
# Solo consultas WQL (SELECT ... FROM clase): un texto que dice "from here" no es una consulta.
$clases = @(Buscar '(?i)"[^"\r\n]*\bSELECT\b[^"\r\n]*\bFROM\s+\w+|"Wmi\w+' | Where-Object { $_ -notmatch '(?i)(FROM\s+|")(WmiMonitorBrightness\w*|WmiSetBrightness)$' })
if ($espacios) { Regla '2.3 WMI solo para el brillo' 'FALLA' $espacios[0] }
elseif ($win32) { Regla '2.3 WMI solo para el brillo' 'FALLA' $win32[0] }
elseif ($clases) { Regla '2.3 WMI solo para el brillo' 'FALLA' $clases[0] }
else { Regla '2.3 WMI solo para el brillo' 'bien' }

# 2.4 DDC/CI: solo el brillo. Nada de VCP a mano ni de otros ajustes del monitor.
Prohibido '2.4 DDC/CI solo el brillo' '\b(SetVCPFeature|SaveCurrentMonitorSettings|RestoreMonitorFactory\w*|SetMonitor(?!Brightness\b)\w+)\s*\('

# 2.5 Luz nocturna: el blob solo se toca en sus dos archivos, el horario solo se lee, y
#     antes de escribir hay copia de seguridad.
$fueraNoche = Buscar '(?i)bluelightreduction' '' '^src\\system\\nightlight(_blob)?\.(cpp|h)$'
$horarioEscrito = Sentencias 'kSettingsPath[^;]*KEY_(SET_VALUE|WRITE|ALL_ACCESS)|KEY_(SET_VALUE|WRITE|ALL_ACCESS)[^;]*kSettingsPath' '^src\\system\\nightlight\.cpp$'
$escribeNoche = Buscar '\bReg(SetValueEx|SetKeyValue)\w*\s*\(' '^src\\system\\nightlight\.cpp$'
$copia = Buscar '"luz-nocturna\.bak"' '^src\\system\\nightlight\.cpp$'
if ($fueraNoche) { Regla '2.5 Luz nocturna con cuidado' 'FALLA' $fueraNoche[0] }
elseif ($horarioEscrito) { Regla '2.5 Luz nocturna con cuidado' 'FALLA' "el horario se abre para escribir: $($horarioEscrito[0])" }
elseif ($escribeNoche -and -not $copia) { Regla '2.5 Luz nocturna con cuidado' 'FALLA' 'se escribe sin copia en luz-nocturna.bak' }
elseif (-not $escribeNoche) { Regla '2.5 Luz nocturna con cuidado' 'pendiente' 'todavia no hay codigo que la escriba (fase 6)' }
else { Regla '2.5 Luz nocturna con cuidado' 'bien' }

# 2.6 Radios: sin emparejar, conectar ni desconectar.
#     WlanConnect y WlanConnect2 son las funciones que conectan; WlanConnectionProfileDetails,
#     de WinRT, solo lee el nombre de la red y no cuenta.
Prohibido '2.6 Radios: solo encender y apagar' '\b(PairAsync|UnpairAsync|BluetoothAuthenticate\w*|BluetoothRemoveDevice|BluetoothSetServiceState|WlanConnect2?|WlanDisconnect)\b'

# --- Salida ------------------------------------------------------------------------

Write-Output ''
Write-Output 'Auditoria del Panel'
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
