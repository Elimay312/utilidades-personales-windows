#pragma once

// Los diálogos de archivo del sistema y el par de llamadas para leer y escribir uno.
//
// Son ventanas del SISTEMA, no controles hijos, así que no chocan con la decisión de la
// fase 1: sin superficie de redirección no se puede tener un HWND hijo dentro de la ventana,
// pero un cuadro de diálogo modal es una ventana aparte y se pinta él solo.
//
// Elegir un archivo es una decisión del usuario y por eso se le pregunta con el cuadro de
// siempre en vez de con una ruta escrita en un ajuste: la copia de seguridad de sus notas
// la guarda donde él quiera, y la carpeta de sus repositorios la sabe él.
//
// Arrepentirse NO es un error. Cerrar el cuadro devuelve una ruta vacía, que es exactamente
// lo que significa: no hay archivo.

#include <Windows.h>

#include <string>
#include <string_view>

#include "model/Result.h"

namespace Shell {

std::wstring PickFolder(HWND owner, const wchar_t* title);
// 'extension' sin el punto: "json". Sirve para el filtro y para ponerla si falta.
std::wstring PickOpenFile(HWND owner, const wchar_t* title, const wchar_t* extension);
std::wstring PickSaveFile(HWND owner, const wchar_t* title, const wchar_t* extension,
                          const wchar_t* suggested);

// No se llaman WriteFile ni ReadFile a propósito: esos son dos funciones de Win32 y
// <Windows.h> está incluido tres líneas más arriba.
Model::Outcome SaveText(const std::wstring& path, std::string_view utf8);
Model::Result<std::string> LoadText(const std::wstring& path);

}  // namespace Shell
