# Seguridad de Brújula

Este documento es de Brújula y solo de Brújula. No es una norma de la casa, igual que
`dock/SEGURIDAD.md` no lo era: un dock mueve ventanas y un lanzador lee el menú Inicio,
y ninguno de los dos se parece a lo que hace esto.

Lo que hace esto es **hablar con la cuenta de GitHub del autor**, que es de trabajo, con
unos 120 repositorios en su mayoría privados. Ninguna otra utilidad de la carpeta maneja
una credencial ni toca datos que no sean del equipo. Así que las reglas van casi todas
sobre lo mismo: **el token y lo que se hace con él**.

Está escrito al terminar la fase 1, cuando todavía no hay una sola línea de red ni de
almacenamiento. Es a propósito: renunciar ahora a algo que no existe es gratis, y dentro
de tres fases no lo será. Se rediseñará al llegar al producto mínimo, que es cuando se
sabrá qué de esto hacía falta de verdad y qué me inventé.

---

## Lo que Brújula no va a hacer

### 1. El token nunca toca el disco en claro

Va al **Administrador de credenciales de Windows** (`CredWriteW` / `CredReadW`) y a ningún
otro sitio. No en un archivo de configuración, no en SQLite, no en una variable de
entorno que se persista, no en el registro.

Si GitHub CLI está instalado y autenticado, se prefiere pedirle el token a él
(`gh auth token`) y no guardarlo: el mejor sitio para una credencial es el de otro que ya
la gestiona bien.

### 2. El token no se registra, ni entero ni a trozos

Ni en el log, ni en un mensaje de error, ni en una excepción, ni en la ventana. Tampoco
"los primeros cuatro caracteres para depurar". Un token con la mitad tapada sigue siendo
la mitad de un token en un archivo de texto.

### 3. No se registran las respuestas de la API

Ni cuerpos de petición, ni cuerpos de respuesta, ni cabeceras. Son nombres, descripciones
y mensajes de commit de repositorios privados de trabajo. Del intercambio se puede anotar
el código de estado, el momento y cuántos repositorios llegaron; el contenido no.

### 4. Nada sale del equipo salvo hacia GitHub

Un único destino: `api.github.com`, por HTTPS. Sin telemetría, sin analítica, sin informes
de fallo remotos, sin comprobación de actualizaciones. Si algún día hace falta abrir un
segundo destino, se escribe aquí antes de escribirlo en el código.

### 5. Escribir en un repositorio es siempre una decisión del usuario

El modo por defecto es **local**: prioridad, estado, siguiente paso y novedades se quedan
en SQLite, en `%LOCALAPPDATA%\Brujula\`, y no se crea ni un commit.

El **modo repo** escribe `PROYECTO.md` en la raíz del repositorio, y antes del primer
commit en **cada** repositorio pide confirmación. No hay un interruptor global que active
el modo repo en los 120 de golpe sin pasar por esa confirmación una vez por repositorio.

Brújula no borra archivos de un repositorio, no toca ramas, no fuerza nada y no escribe
más archivo que `PROYECTO.md`.

### 6. Permisos mínimos, y crecen solo cuando el usuario lo pide

El token que se pide es *fine-grained* con **Metadata: read**. *Contents: read and write*
solo se pide si el usuario activa el modo repo, y la ventana que lo explica dice para qué
es antes de pedirlo.

### 7. No se ejecuta nada que venga de la red

Ni scripts, ni binarios, ni extensiones. SQLite se compila con
`SQLITE_OMIT_LOAD_EXTENSION`, para que un archivo de base de datos no pueda cargar un
`.dll` por su cuenta. Lo que llega de GitHub es texto que se enseña, nunca algo que se
ejecuta.

### 8. Sin privilegios y sin quedarse detrás

El manifiesto pide `asInvoker`. Brújula no se instala como servicio, no deja un proceso
al cerrarse y no se pone en el arranque de Windows sin que se lo pidan.

### 9. Las dependencias están fijadas y son pocas

Las cuatro que hay —nlohmann/json, SQLite, doctest y lo que traiga el SDK— entran por
`FetchContent` con la versión fijada, y SQLite además con el hash SHA-256 de su archivo.
Nada apunta a una rama. Una dependencia nueva se consulta antes.

---

## Cómo se comprueba

`auditar.ps1` recorre el código **quitando primero los comentarios** —porque este mismo
documento y los comentarios que lo citan están llenos de las palabras prohibidas— y
devuelve 0 si todo está limpio y 1 si no.

```
powershell -NoProfile -ExecutionPolicy Bypass -File auditar.ps1
```

Una lista de prohibiciones que nadie comprueba es una carta de intenciones. Las reglas
que el script todavía no sabe mirar, porque el código que vigilarían no existe, están
marcadas en su salida como pendientes en vez de darse por buenas.
