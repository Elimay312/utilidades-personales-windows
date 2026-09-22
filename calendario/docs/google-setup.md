# Conectar Agenda con tu cuenta de Google

Las credenciales las creas **tú**, en tu propia cuenta. Agenda no trae ninguna y nunca se
guarda un `client_secret` en el repositorio: `config.local.json`, `client_secret*.json` y
`token.bin` están en `.gitignore`. Además, nada de esto vive en la carpeta del proyecto: las
credenciales y el token van a `%LOCALAPPDATA%\Agenda`.

Son quince minutos y se hacen una sola vez. Al final tendrás dos cadenas que pegar en un
archivo.

---

## 1. Crea el proyecto

1. Entra en [console.cloud.google.com](https://console.cloud.google.com/).
2. Arriba, en el selector de proyectos, **Proyecto nuevo**.
3. Nombre: `Agenda` (da igual, solo lo ves tú). Sin organización.
4. **Crear**, y espera a que el selector cambie a ese proyecto antes de seguir. Todo lo demás
   se hace dentro de él, y es el error más fácil de cometer: activar las API en el proyecto
   equivocado.

## 2. Activa las dos API

En **API y servicios → Biblioteca**, busca y activa estas dos:

- **Google Calendar API**
- **Google Tasks API**

Son dos activaciones separadas. Si falta la de Tasks, los eventos sincronizarán y las tareas
devolverán un 403 que no dice gran cosa.

## 3. Pantalla de consentimiento

En **API y servicios → Pantalla de consentimiento de OAuth**:

1. Tipo de usuario: **Externo**. «Interno» solo existe si tienes Google Workspace.
2. Nombre de la aplicación: `Agenda`. Correo de asistencia y de contacto: el tuyo.
3. En **Permisos**, añade estos tres y ninguno más:

   ```
   https://www.googleapis.com/auth/calendar.events
   https://www.googleapis.com/auth/calendar.readonly
   https://www.googleapis.com/auth/tasks
   ```

   `calendar.readonly` está solo para leer **la lista de calendarios**, que es de donde salen
   los nombres y los colores. Agenda no pide `calendar` a secas, que daría permiso para borrar
   calendarios enteros.

4. **Publica la aplicación** (botón «Publicar aplicación», estado *En producción*).

   Esto importa más de lo que parece. Si la dejas en *Prueba*, Google **caduca el token de
   actualización a los siete días** y hay que volver a dar permiso cada semana. Publicada, el
   token dura hasta que lo revoques.

   **Publicar no exige dominio, página principal ni política de privacidad.** Si el botón está
   desactivado y pone «para publicar tu app, debes completar la configuración en la página de
   desarrollo de la marca», lo que falta es un campo obligatorio de esa página, y casi siempre
   es el último: **Información de contacto del desarrollador → Direcciones de correo
   electrónico**. Pon el tuyo y guarda. Deja **vacíos** «Dominio de la app» y «Dominios
   autorizados»: esos solo hacen falta si envías la app a verificación, que es otra cosa y que
   tú no necesitas.

   **No subas el logotipo.** La propia página lo avisa: en cuanto subes uno, la app tiene que
   pasar por verificación. Es la única trampa de esa pantalla.

   Como la aplicación no está verificada, al dar permiso verás una pantalla de advertencia
   —«Google no ha verificado esta aplicación»—. Es lo normal para algo que has creado tú para
   ti: entra en **Configuración avanzada → Ir a Agenda (no seguro)**. La verificación solo sirve
   para quitar ese aviso y repartir la app a desconocidos; el límite sin verificar son 100
   usuarios, y aquí el usuario eres tú.

   **Si aun así te quedas en *Prueba*, Agenda no se rompe en silencio.** Cuando Google deje de
   renovar el permiso, un globo en la bandeja lo dice y el menú vuelve a ofrecer «Conectar con
   Google…». Reconectar son dos clics, porque la sesión y el consentimiento ya están dados. Lo
   que hayas escrito entretanto sigue guardado y sube en cuanto vuelvas a dar permiso.

## 4. Crea las credenciales

En **API y servicios → Credenciales → Crear credenciales → ID de cliente de OAuth**:

1. Tipo de aplicación: **Aplicación de escritorio**.
2. Nombre: `Agenda`.
3. **Crear**. Google te enseña el **ID de cliente** y el **Secreto de cliente**. Déjalos a mano.

No hay que configurar ninguna URI de redirección: el tipo «Aplicación de escritorio» ya
autoriza `http://127.0.0.1` en cualquier puerto, que es justo lo que Agenda usa —levanta un
oyente en un puerto libre y lo cierra en cuanto recibe la respuesta—.

El «secreto» de una aplicación de escritorio no es un secreto de verdad: cualquiera que tenga
el ejecutable lo tiene. Lo que protege el intercambio es **PKCE**, que Agenda implementa, y por
eso ese valor no da acceso a nada por sí solo. Aun así no va al repositorio.

## 5. Pega las dos cadenas

En el repositorio hay una plantilla, [`config.example.json`](../config.example.json), con todas
las claves y sus valores por defecto. **Ábrela, cópiala y guárdala como:**

```
%LOCALAPPDATA%\Agenda\config.local.json
```

que en esta máquina es `C:\Users\elima\AppData\Local\Agenda\config.local.json`. La carpeta ya
existe, porque ahí vive la base de datos.

Lo mínimo que hace falta es el bloque `google`; el resto puede faltar:

```json
{
  "google": {
    "clientId": "000000000000-xxxxxxxxxxxxxxxxxxxxxxxxxxxx.apps.googleusercontent.com",
    "clientSecret": "GOCSPX-xxxxxxxxxxxxxxxxxxxxxxxx"
  }
}
```

La plantilla se versiona porque no tiene nada dentro; **`config.local.json` no**, y además no
vive en la carpeta del proyecto. Guárdalo en **UTF-8 sin BOM**: con BOM el JSON no parsea y
Agenda arranca como si no hubiera credenciales.

Si el archivo ya existe con otras claves —el atajo, por ejemplo—, **añade** el bloque `google`
en vez de reemplazar el contenido: Agenda fusiona `config.json` y `config.local.json`, así que
lo que borres de aquí desaparece.

Un JSON mal formado no rompe la aplicación: se registra en el log y se ignora, lo que significa
que **si te falta una coma, Agenda arranca como si no hubiera credenciales**. Si «Conectar con
Google…» no aparece en el menú de la bandeja, mira el log del día en
`%LOCALAPPDATA%\Agenda\logs\`.

## 6. Conecta

1. Abre Agenda.
2. Clic derecho en el icono de la bandeja → **Conectar con Google…**.
3. Un diálogo avisa de que se va a abrir el navegador. Confirma.
4. Elige la cuenta, pasa la pantalla de «no verificada» y acepta los permisos.
5. La pestaña dirá que ya puedes cerrarla. La primera sincronización empieza sola.

El token de actualización se guarda cifrado con **DPAPI** en
`%LOCALAPPDATA%\Agenda\token.bin`. Va atado a tu usuario de Windows: copiar ese archivo a
otro equipo o a otra cuenta no sirve de nada, hay que volver a conectar.

---

## Si algo va mal

| Lo que ves | Qué es |
|---|---|
| `access_denied` en el navegador | No pasaste la pantalla de «aplicación no verificada», o tu cuenta no está en la lista de usuarios de prueba y la app sigue en modo *Prueba*. |
| `invalid_client` | El `clientId` o el `clientSecret` están mal copiados, o las credenciales son de otro proyecto. |
| Un globo que dice «se acabó el permiso de Google» | El token de actualización caducó o lo revocaste. Casi siempre es lo primero, y significa que la app sigue en modo *Prueba* (paso 3.4). Vuelve a conectar desde la bandeja; nada de lo escrito se ha perdido. |
| El botón «Publicar aplicación» está desactivado | Falta un campo obligatorio en «Información de marca», normalmente el correo de contacto del desarrollador (paso 3.4). No es por los dominios. |
| Los eventos sincronizan y las tareas no | Falta activar **Google Tasks API** (paso 2). |
| El indicador de sin conexión no se apaga | No es Google: no hay red, o un proxy está cortando la salida. Lo creado no se pierde, sube cuando vuelva. |

## Cómo desconectar

Clic derecho en el icono de la bandeja → **Desconectar de Google**. Eso borra
`%LOCALAPPDATA%\Agenda\token.bin`, que es donde vive el permiso. Si además quieres retirárselo
del lado de Google, entra en
[myaccount.google.com/permissions](https://myaccount.google.com/permissions) y quita `Agenda`.

La caché local se queda como estaba: nada de lo que has creado desaparece, y sigue ahí si
vuelves a conectar.
