#pragma once

// La versión, en UN solo sitio.
//
// La lee el .rc —que sí sabe incluir cabeceras, mientras no tengan C++ dentro— y la hoja de
// «Acerca de». Con el número escrito en los dos, el día que se suba uno y no el otro no
// falla nada: simplemente las propiedades del archivo dicen una cosa y la aplicación otra,
// y nadie se entera hasta que hace falta saber qué versión tiene alguien delante.
//
// Solo #define y nada de C++: este archivo pasa por rc.exe.

#define BRUJULA_VERSION_MAJOR 0
#define BRUJULA_VERSION_MINOR 8
#define BRUJULA_VERSION_PATCH 0

// La misma, ya escrita, porque el preprocesador de rc.exe no sabe pegar números.
#define BRUJULA_VERSION_STR "0.8.0"
#define BRUJULA_VERSION_WSTR L"0.8.0"

// El identificador del icono. El 1 no es casual: el shell enseña como icono del ejecutable
// el grupo de iconos con el ID numérico más bajo, así que este tiene que ser el primero.
#define BRUJULA_ICON 1
