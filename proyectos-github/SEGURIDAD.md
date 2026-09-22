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

*Anotado en la fase 3, porque tiene un precio:* el token que devuelve GitHub CLI es uno
clásico —en esta máquina, `repo, read:org, gist`— y eso es muchísimo más ancho que el
*fine-grained* que pide la regla 6. Se acepta a sabiendas: la credencial ya existe, ya la
gestiona otro y no la acuñamos nosotros. Lo que sí se hace es leer `X-OAuth-Scopes` al
validarla y guardarlo, para poder enseñar en los ajustes qué permisos tiene de verdad la
que se está usando.

### 2. El token no se registra, ni entero ni a trozos

Ni en el log, ni en un mensaje de error, ni en una excepción, ni en la ventana. Tampoco
"los primeros cuatro caracteres para depurar". Un token con la mitad tapada sigue siendo
la mitad de un token en un archivo de texto.

### 3. No se registran las respuestas de la API

Ni cuerpos de petición, ni cuerpos de respuesta, ni cabeceras. Son nombres, descripciones
y mensajes de commit de repositorios privados de trabajo. Del intercambio se puede anotar
el código de estado, el momento y cuántos repositorios llegaron; el contenido no.

*La fase 3 decidió no tener log, y esta regla es el motivo.* Un archivo de registro que
nadie mira y que va llenándose de lo que contesta la API es justo lo que esta regla intenta
evitar, así que el papel lo hace la fila de estado de SQLite: cuándo fue la última
sincronización, cuántos repositorios, qué protocolo y el último error ya redactado.

Y no es solo una costumbre: el tipo `Model::Error` **no tiene dónde meter** un cuerpo de
respuesta. Su campo `detail` es una frase que redactamos nosotros. Lo que sí lleva es el
`X-GitHub-Request-Id`, que identifica el intercambio ante GitHub sin contener nada de
dentro.

### 4. Nada sale del equipo salvo hacia GitHub

Un único destino al que Brújula habla: `api.github.com`, por HTTPS. Sin telemetría, sin
analítica, sin informes de fallo remotos, sin comprobación de actualizaciones.

*Y la excepción que la fase 3 escribe aquí antes de escribirla en el código:* la hoja de
bienvenida abre `https://github.com/settings/personal-access-tokens/new` **en el navegador
del sistema**, que es donde se crea el token. Brújula no pide esa página ni la lee: se la
pasa a Windows. Sigue sin haber un segundo sitio con el que Brújula hable.

La fase 4 querrá los avatares, que están en `githubusercontent.com`. Eso sí sería un
segundo destino de verdad, y se escribe aquí cuando toque.

### 5. Escribir en un repositorio es siempre una decisión del usuario

El modo por defecto es **local**: prioridad, estado, siguiente paso y novedades se quedan
en SQLite, en `%LOCALAPPDATA%\Brujula\`, y no se crea ni un commit.

El **modo repo** escribe `PROYECTO.md` en la raíz del repositorio, y antes del primer
commit en **cada** repositorio pide confirmación. No hay un interruptor global que active
el modo repo en los 120 de golpe sin pasar por esa confirmación una vez por repositorio.

Brújula no borra archivos de un repositorio, no toca ramas, no fuerza nada y no escribe
más archivo que `PROYECTO.md`.

### 6. Permisos mínimos, y crecen solo cuando el usuario lo pide

El token que se pide es *fine-grained* con **Metadata: read** y **Contents: read**, y la
hoja que los explica dice para qué es cada uno antes de pedirlos.

*Contents: read* entró en la fase 3 al medirlo: leer `PROYECTO.md` es leer contenido del
repositorio, y con *Metadata* a secas ese campo vuelve vacío en los 109. Se pidió la mínima
ampliación que hacía falta —lectura, no escritura— y la sincronización degrada sola: si la
credencial no llega, repite la tanda sin ese campo y lo anota, en vez de no sincronizar.

*Contents: **write*** sigue siendo solo del modo repo, y sigue confirmándose repositorio
por repositorio. Esa parte de la regla no se ha tocado.

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

### 10. Lo que llega de fuera del equipo no manda

Desde la fase 8 hay una puerta que no abre el usuario: el esquema `brujula://`. Un esquema
propio lo puede disparar **cualquier página web** —el navegador pregunta, y hay gente que
dice que sí— y el `WM_COPYDATA` con el que una segunda instancia entrega lo que abrir se lo
puede mandar cualquiera que sepa el nombre de nuestra clase de ventana.

Lo que entra por ahí es **un nombre de repositorio o nada**. Se filtra con una lista blanca
—letras, cifras y `-_./`, una sola barra, sin empezar por punto, tope de 200 caracteres— que
vive en `App::LooksLikeRepoName`, es pura, está en `brujula_core` y tiene pruebas con
`../../Windows/System32` dentro. Se valida en los **dos** caminos de entrada, no solo en el
que viene de nuestro propio `wWinMain`.

Y se usa para una sola cosa: buscar ese nombre en la caché. No abre archivos, no compone
rutas y no llega a ninguna petición de red. Si algún día llegara a alguna de esas tres, esta
regla es la que habría que volver a leer.

### 11. El registro solo se toca dentro de la clave de Brújula

Registrar `brujula://` escribe en `HKCU\Software\Classes\brujula` y en ningún otro sitio.
En HKCU y no en HKLM porque Brújula es un `.exe` que se copia y no un instalador — HKLM
pediría elevación, que la regla 8 prohíbe. Se lee antes de escribir, así que un arranque
normal no toca el registro. Y no se borra al salir: un esquema que solo funciona con la
aplicación abierta no sirve para abrirla.

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
