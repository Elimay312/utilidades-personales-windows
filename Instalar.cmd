@echo off
rem Doble clic y listo: instala git si falta, baja el repo si hace falta, instala todas las
rem utilidades con sus dependencias y deja programada la actualizacion al iniciar sesion.
rem Sirve dentro del repo o suelto (descargado solo este archivo).
setlocal
chcp 65001 >nul

set "REPO=%~dp0"
if exist "%REPO%actualizar.ps1" goto instalar

set "REPO=%USERPROFILE%\utilidades-personales-windows\"
if exist "%REPO%actualizar.ps1" goto instalar

where git >nul 2>&1 || (
    echo Instalando git...
    winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements
    set "PATH=%PATH%;%ProgramFiles%\Git\cmd"
)
git clone https://github.com/Elimay312/utilidades-personales-windows.git "%REPO:~0,-1%" || goto error

:instalar
powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO%actualizar.ps1" -Todo
set "FALLO=%ERRORLEVEL%"
powershell -NoProfile -ExecutionPolicy Bypass -File "%REPO%actualizar.ps1" -Programar
if not "%FALLO%"=="0" goto error
echo.
echo Listo. Todo instalado y se actualizara solo al iniciar sesion.
pause
exit /b 0

:error
echo.
echo Algo fallo. Mira el mensaje de arriba o %LOCALAPPDATA%\Utilidades\actualizar.log
echo y vuelve a hacer doble clic para reintentar.
pause
exit /b 1
