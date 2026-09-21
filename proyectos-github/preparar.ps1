<#
    Deja la sesión lista para compilar Brújula y, si no se pide lo contrario, compila.

    En esta máquina ni cmake ni ninja están en el PATH: viven dentro de Visual Studio
    Build Tools. Así que el proyecto se busca su entorno solo, igual que el lanzador y la
    isla se buscan el SDK de .NET, y por el mismo motivo: un README que dice "abre la
    consola correcta" es una instrucción que se olvida, y el error que provoca —"cmake no
    se reconoce"— no se parece en nada a su causa.

        powershell -NoProfile -ExecutionPolicy Bypass -File preparar.ps1
        powershell -NoProfile -ExecutionPolicy Bypass -File preparar.ps1 -Configuracion Debug
        powershell -NoProfile -ExecutionPolicy Bypass -File preparar.ps1 -SoloEntorno

    Con punto delante, el entorno se queda en la consola actual y después funcionan los
    comandos de CLAUDE.md tal cual están escritos:

        . .\preparar.ps1 -SoloEntorno
        cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#>
# Este archivo se guarda en UTF-8 CON BOM, y hace falta: Windows PowerShell 5.1 —el que
# arranca al escribir "powershell"— lee los .ps1 sin BOM como ANSI, y entonces cada acento
# de los mensajes sale convertido en dos caracteres raros. Los vecinos lo esquivan
# escribiendo sin acentos; aqui se prefiere el BOM y escribir en espanol entero.

param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuracion = 'Release',
    [string]$Directorio = 'build',
    [switch]$SoloEntorno
)

$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$script:Instalador = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'

function Hay([string]$Programa) {
    return [bool](Get-Command $Programa -ErrorAction SilentlyContinue)
}

function Buscar-VisualStudio {
    # vswhere es la única manera soportada de encontrar Visual Studio: la ruta de Program
    # Files cambió de sitio entre ediciones (estas Build Tools están en la de x86) y el
    # registro ya no lleva las instalaciones desde 2017.
    $vswhere = Join-Path $script:Instalador 'vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }

    # -products * porque aquí solo hay Build Tools, que no sale en la búsqueda por
    # defecto. -requires descarta instalaciones sin compilador de C++ x64.
    $ruta = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath 2>$null
    if (-not $ruta -or -not (Test-Path $ruta)) { return $null }
    return "$ruta"
}

function Importar-Entorno([string]$VisualStudio) {
    $devcmd = Join-Path $VisualStudio 'Common7\Tools\VsDevCmd.bat'
    if (-not (Test-Path $devcmd)) {
        throw "Visual Studio está en $VisualStudio pero le falta VsDevCmd.bat. Repáralo desde el Instalador de Visual Studio."
    }

    # VsDevCmd deja el entorno en SU proceso, así que hay que pedirle que lo imprima y
    # copiarlo aquí. Se usa VsDevCmd y no vcvars64 porque vcvars64 monta el compilador
    # pero NO añade CMake ni Ninja al PATH, que es justo lo que falta en esta máquina.
    #
    # El PATH lleva delante la carpeta del Instalador, y es una corrección medida: dentro
    # VsDevCmd hace "pushd" a esa carpeta y llama a "vswhere.exe" a secas, contando con
    # que cmd busque en el directorio actual. Con NoDefaultCurrentDirectoryInExePath=1 en
    # el entorno —lo está en esta máquina— cmd deja de buscar ahí y suelta un
    # «"vswhere.exe" no se reconoce» que no menciona ni a VsDevCmd ni a la variable.
    $salida = cmd /c "set `"PATH=$script:Instalador;%PATH%`" && `"$devcmd`" -arch=x64 -host_arch=x64 -no_logo && set" 2>&1
    if ($LASTEXITCODE -ne 0) { throw "VsDevCmd.bat falló con código $LASTEXITCODE." }

    foreach ($linea in $salida) {
        if ("$linea" -match '^([^=]+)=(.*)$') {
            Set-Item -Path "Env:\$($Matches[1])" -Value $Matches[2]
        }
    }
}

function Asegurar-Herramienta([string]$Programa, [string[]]$Candidatos, [string]$Pista) {
    if (Hay $Programa) { return }

    foreach ($candidato in $Candidatos) {
        if ($candidato -and (Test-Path $candidato)) {
            $env:PATH = "$(Split-Path -Parent $candidato);$env:PATH"
            if (Hay $Programa) { return }
        }
    }
    throw "No encuentro $Programa. $Pista"
}

# --- 1. El compilador --------------------------------------------------------------

$vs = Buscar-VisualStudio

if (Hay 'cl') {
    Write-Output 'MSVC: ya estaba en la sesión.'
} else {
    if (-not $vs) {
        throw @'
No encuentro Visual Studio con las herramientas de C++ x64. Instala "Build Tools para
Visual Studio 2022" con la carga de trabajo "Desarrollo para el escritorio con C++":

    winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
'@
    }
    Importar-Entorno $vs
    if (-not (Hay 'cl')) { throw "Importé el entorno de $vs pero cl.exe sigue sin aparecer." }
    Write-Output "MSVC: $vs"
}

# --- 2. CMake y Ninja --------------------------------------------------------------

# VsDevCmd los añade cuando el componente "CMake para C++" está instalado; si no, se
# buscan en su sitio dentro de Visual Studio antes de rendirse.
$dentro = if ($vs) { Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake' } else { $null }

Asegurar-Herramienta 'cmake' @(
    $(if ($dentro) { Join-Path $dentro 'CMake\bin\cmake.exe' }),
    (Join-Path ${env:ProgramFiles} 'CMake\bin\cmake.exe')
) 'Añade el componente "CMake para C++" desde el Instalador de Visual Studio, o instala CMake y ponlo en el PATH.'

Asegurar-Herramienta 'ninja' @(
    $(if ($dentro) { Join-Path $dentro 'Ninja\ninja.exe' })
) 'Añade el componente "CMake para C++" desde el Instalador de Visual Studio, que trae Ninja.'

Write-Output "CMake: $((Get-Command cmake).Source)"
Write-Output "Ninja: $((Get-Command ninja).Source)"

if ($SoloEntorno) {
    Write-Output ''
    Write-Output 'Entorno listo. Si ejecutaste el script con punto delante, ya puedes compilar:'
    Write-Output "    cmake -S . -B $Directorio -G Ninja -DCMAKE_BUILD_TYPE=$Configuracion"
    Write-Output "    cmake --build $Directorio"
    return
}

# --- 3. Compilar -------------------------------------------------------------------

Write-Output ''
# Entrecomillado, y no es cosmetica: PowerShell no expande la variable dentro de un
# token que empieza por guion, asi que sin las comillas a CMake le llega la cadena
# "$Configuracion" tal cual y acaba en CMAKE_BUILD_TYPE. El error que suelta despues es de
# Ninja quejandose de una regla mal formada, que no menciona ni PowerShell ni el guion.
cmake -S . -B $Directorio -G Ninja "-DCMAKE_BUILD_TYPE=$Configuracion"
if ($LASTEXITCODE -ne 0) { throw "La configuración de CMake falló con código $LASTEXITCODE." }

cmake --build $Directorio
if ($LASTEXITCODE -ne 0) { throw "La compilación falló con código $LASTEXITCODE." }

Write-Output ''
Write-Output "Listo: $Directorio\brujula.exe y $Directorio\brujula_tests.exe"
