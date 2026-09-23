# Changelog

Formato [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/), versiones SemVer.

## [Unreleased]

### Añadido

- **Fase 8: la entrega.**
  - **`Instalar-Panel.exe`,** el instalador de Agenda adaptado. Es un solo archivo con Panel.exe
    dentro, instala por usuario en `%LOCALAPPDATA%\Programs\Panel` y se deja a sí mismo como
    `Desinstalar.exe`, con acceso en el menú Inicio y entrada en Aplicaciones instaladas.
    `empaquetar.ps1` lo compila. La enmienda §2.8 de `SEGURIDAD.md` va en su propio commit,
    antes que el código. Estos son los cambios respecto al de Agenda:
    - **Un Panel abierto** recibe `WM_CLOSE` en su ventana oculta, y solo si es el instalado. Sin
      el `TerminateProcess` de respaldo del de Agenda: si en 5 s no se ha cerrado, lo dice y no
      sigue. Un Panel de otra carpeta, como el del build, no se toca.
    - **Se desinstala sin `cmd.exe`:** una copia del desinstalador en `%TEMP%` espera a que
      salga el original y borra la carpeta, que calcula ella misma.
    - **Panel se abre con `CreateProcessW`,** no con `ShellExecute`.
  - **La ventana oculta del Panel** ahora sale al recibir `WM_CLOSE`: `WM_DESTROY` pone el
    `PostQuitMessage`. Antes, `DefWindowProc` la destruía y el proceso seguía vivo sin atajo.
  - **«Iniciar con Windows»** en el menú del clic derecho: el valor `Panel` de `Run`, con el
    `core/autostart.h` de Agenda. El valor es la única verdad, y el menú lo lee al abrirse.
  - **Auditoría:**
    - la 1.6 ahora prohíbe escribir intérpretes (`cmd`, `rmdir`, PowerShell) en el código;
    - la 1.5 y la 1.6 dejan al instalador su `WM_CLOSE` y su `CreateProcessW`;
    - cada cambio se probó con código que la incumple.

    El instalador escribe su archivo con `std::ofstream`, porque la 2.6 guarda `CreateFile` para
    los dispositivos de audio Bluetooth y no hacía falta aflojarla.
  - **Probado en una carpeta de pruebas** (`LOCALAPPDATA` apuntando a ella y
    `PANEL_INSTALLER_NO_REGISTRY=1`). Se hicieron tres cosas y todo salió bien:
    - instalar;
    - actualizar con el Panel instalado abierto: se cerró con código 0 y el log dice `exit`;
    - desinstalar con el Panel abierto: se cerró, y se borraron la carpeta y el acceso.
  - **Medido con la Release:**
    - escondido: 1,7–4,6 MB de memoria de trabajo y 0–16 ms de CPU cada 10 s;
    - abierto y quieto: 13,9 MB y 0 ms de CPU;
    - en aparecer, 17–30 ms.

    La memoria privada comprometida ronda los 55 MB, la mayoría reservas del driver de D3D que
    no ocupan RAM.

- **Fase 7: las utilidades de verdad.**
  - **La fila** enseña las utilidades que viven en segundo plano: Dock, Isla, HUD, QuickLook,
    Lanzador y ahora también Agenda. Salen de `utilidades` en `panel.json` (nombre, exe, icono y
    clase de ventana), con esas seis por defecto. Una utilidad nueva es una línea de JSON.
  - **Qué está en marcha:** una instantánea de Toolhelp en cada apertura, en el hilo de trabajo.
    Solo cuenta un proceso cuya ruta completa es la configurada; otro programa con el mismo nombre
    de archivo no.
  - **Arrancar:** `CreateProcessW` con la ruta como nombre de aplicación y sin línea de comandos,
    solo si es absoluta, existe y termina en `.exe`.
  - **Cerrar:** `WM_CLOSE` a las ventanas de nivel superior de su clase y de ese proceso. En el
    Dock, las de todas las pantallas. En Agenda, su ventana de aplicación y no el popup. En
    QuickLook, la anfitriona y no el panel de vista previa. Después espera hasta 3 s; si sigue
    abierta, lo dice y no la fuerza.
  - **Por qué no el Restart Manager del plan:** ninguna utilidad atiende los mensajes de fin de
    sesión, así que `RmShutdown` sin forzar no cerraría ninguna. La enmienda a `SEGURIDAD.md`
    §1.5 y §2.7 va en su propio commit, antes que el código, con la regla de auditoría «solo
    `apps.cpp` manda `WM_CLOSE` a otra ventana».
  - **Auditoría:** el detalle de la regla nueva señalaba el archivo equivocado, por cómo pasa
    PowerShell un array por la tubería. Encontrado con la sonda de violaciones y arreglado.
  - **Pruebas:** 4 casos nuevos para rutas, iconos y la configuración. 47 en total.
  - **Probado con QuickLook**, que estaba parado: arrancado y cerrado desde el panel, varias
    veces, y quedó parado. Las demás utilidades, que el usuario estaba usando, no se tocaron.
  - **Una falsa alarma que queda anotada:** en una captura parecía faltar el punto verde de la
    utilidad con el ratón encima. Los píxeles decían que el verde estaba. A ojo, la imagen
    reducida engaña; hay que medir.

- **Fase 6: la luz nocturna de verdad.**
  - **`system/nightlight_blob.cpp`:** un lector y escritor Bond CompactBinary v1, puro, que
    conserva byte a byte cada campo que no entiende. Lee el sobre de CloudStore y dentro el
    estado (el campo 0 presente quiere decir encendida) y el horario (activado o no, horas fijas
    o de la puesta al amanecer, inicio y fin). El formato está documentado por
    kvnxiao/win-nightlight-cli.
  - **`system/nightlight.cpp`:** es el único que toca el registro para esto.
    - Lee los dos valores y vigila sus cambios con `RegNotifyChangeKeyValue` y una espera del
      pool de Windows, sin hilo propio. Así el tile sigue al interruptor de Windows.
    - **Solo escribe el valor del estado**, releído justo antes. Si no hace la ida y vuelta
      exacta, no escribe y el tile dice «No compatible».
    - La primera vez guarda el original en `%LOCALAPPDATA%\Panel\luz-nocturna.bak`, y solo esa
      vez.
  - **El tile:** enciende y apaga. Debajo dice «Encendida», «Hasta 07:00», la hora a la que se
    enciende sola o «Apagada».
  - **Auditoría:** la regla 2.5 deja de estar pendiente y da «bien». Por primera vez sale
    **TODO LIMPIO** sin reglas pendientes.
  - **Pruebas:** 5 casos nuevos con los cuatro blobs reales de esta máquina y el ejemplo
    documentado: ida y vuelta exacta, lectura, encender y apagar, y rechazar un blob raro. 43 en
    total.
  - **Probado en este equipo:**
    - encender desde el panel añade exactamente `10 00` al blob interno, y apagar lo devuelve a
      la forma original;
    - el usuario vio la pantalla calentarse;
    - un cambio escrito desde fuera llega al tile en los dos sentidos;
    - las claves `…perdevice` no cambian, y ya consta en `SEGURIDAD.md` §2.5.
  - **Al terminar,** la luz nocturna quedó apagada, como estaba.

- **Fase 5b-3: los dispositivos Bluetooth de verdad.**
  - **Los dos `DeviceWatcher` siguen ahora a los emparejados**, con su nombre, si están
    conectados (`System.Devices.Aep.IsConnected`) y su clase. La clase sale del Class of Device
    clásico o de la apariencia LE: audio, teclado, ratón u otro. El recuento del tile sale de
    esta lista.
  - **Una fila por dispositivo**, aunque hable clásico y LE: se juntan por su dirección, y la
    cara clásica pone el nombre y la clase. Van primero los conectados.
  - **Un clic en un dispositivo de audio lo conecta o lo desconecta** (`system/bt_audio.cpp`).
    Es lo que hace el botón «Conectar» de la configuración de sonido: las propiedades de un solo
    uso del driver de audio Bluetooth (`KSPROPERTY_ONESHOT_RECONNECT` y `_DISCONNECT`), por
    `IOCTL_KS_PROPERTY`, a los filtros `KSCATEGORY_AUDIO` que llevan su dirección en la ruta
    (`AudioFilterOf`, con pruebas). Mientras tanto la fila dice «Un momento…». Si a los 12 s
    no ha cambiado (unos auriculares en su estuche no contestan), deja de decirlo.
  - **Teclados, ratones y lo demás** enseñan su estado pero no se pulsan: se conectan solos al
    encenderlos.
  - **«Añadir un dispositivo»** abre la pantalla de Windows
    (`ms-settings-connectabledevices:devicediscovery`). El panel no empareja ni desempareja.
  - La enmienda a `SEGURIDAD.md` §1.6 y §2.6 va en su propio commit, antes que el código, con
    sus reglas de auditoría: un solo archivo, un solo ioctl y dos propiedades.
  - **Pruebas:** 2 casos nuevos, 38 en total.
  - **Probado con unos soundcore P31i**, con permiso del usuario:
    - la lista enseña «soundcore P31i» con icono de auriculares y «BT5.0 KB» con icono de
      teclado;
    - el clic en el teclado no hace nada;
    - desconectar y reconectar desde el panel funciona las dos veces, y Windows lo confirma.
  - **Efecto de desconectar:** Windows mandó el audio a «Altavoces (Steam Streaming Speakers)»
    y lo devolvió a «Auriculares» al reconectarse. Eso lo decide Windows; el panel lo enseña en
    la tarjeta del volumen.

- **Fase 5b-2: las redes Wi-Fi de verdad.**
  - **Al desplegar la tarjeta de Wi-Fi se buscan las redes de alrededor** con WlanAPI
    (`system/wifi.cpp`, en el hilo de trabajo). Primero sale lo que Windows ya sabe y después se
    pide una búsqueda nueva. El aviso de búsqueda terminada llega por la fuente ACM y vuelve a
    leer la lista.
  - **Una fila por red** (`MergeNetworks`, con pruebas): Windows lista dos veces una red
    guardada y deja sin nombre las ocultas. Van primero la que está en uso, luego las
    guardadas, luego por señal. La señal se pasa a las barras de Windows (`BarsFromQuality`).
  - **Un clic en una red guardada** conecta con `WlanConnect` y su perfil. **Una red nueva**, o
    el pie «Más redes en Windows», abre la lista de Windows (`ms-availablenetworks:`), porque la
    contraseña es cosa de Windows. El panel no crea, no cambia y no borra perfiles.
  - **Ubicación:** la búsqueda empieza con la tarjeta y se para al plegarla o al esconder el
    panel. Sin permiso de ubicación, la tarjeta lo dice en una línea.
  - La enmienda a `SEGURIDAD.md` §1.2, §1.6 y §2.6 va en su propio commit, antes que el código,
    con una regla de auditoría por cada corte. Se probó cada una con código que la incumple.
  - **Pruebas:** 3 casos nuevos, 36 en total.
  - **Probado en este equipo:**
    - la tarjeta lista las redes reales, con «CM-AUTOS-5G» arriba como conectada;
    - Windows no enseñó ningún aviso, porque la ubicación ya está permitida para las apps de
      escritorio. Anotó el uso del panel solo en el momento de desplegar;
    - con el panel escondido no volvió a usarla.
  - **Sin probar:** cambiar a otra red guardada. De las 9 redes guardadas, la única al alcance
    era la que ya estaba en uso.

- **Fase 5b-1: Wi-Fi y Bluetooth se despliegan con un morph, todavía con listas de ejemplo.**
  - **La franja «›»:** los tiles de Wi-Fi y Bluetooth llevan una franja a la derecha. El clic en
    el tile sigue encendiendo y apagando, y la franja despliega. Las dos franjas son paradas del
    Tab.
  - **El morph:** el tile se convierte en una tarjeta a todo el ancho, con un muelle de 320 ms
    de periodo y amortiguación 0,82.
    - Posición y tamaño van a la vez. El tile de Bluetooth, que está a la derecha, crece hacia
      la izquierda.
    - El color del tile pasa al de tarjeta antes del 60 % del recorrido.
    - La cara del tile viaja pegada a la esquina de la forma y se va antes del 40 %.
    - El contenido de la tarjeta entra desde el 45 %, pegado a la misma esquina y recortado por
      la forma. Así una tarjeta que aún crece enseña el principio de cada línea, no el final.
    - Los otros tres tiles se funden debajo antes de la mitad. Lo que hay más abajo se desliza, y
      el panel crece hacia arriba desde la esquina.
  - **La tarjeta:**
    - una cabecera con el chevron hacia abajo, que la pliega;
    - el interruptor de la radio, que funciona de verdad;
    - hasta seis redes (con su señal, el candado y «Conectado») o dispositivos (auriculares,
      teclado, ratón);
    - un pie que dice lo que se deja a Windows: «Más redes en Windows» o «Añadir un
      dispositivo»;
    - sin nada que listar, una línea dice por qué: la radio está apagada, no hay permiso de
      ubicación, se está buscando o no hay nada.
  - **`Esc` va por capas,** como en Agenda: primero pliega la tarjeta y después esconde el panel.
  - **Esquema:** `PanelState` gana `wifi.networks[]`, `wifi.scanning`,
    `wifi.locationDenied` y `bluetooth.devices[]`. Solo se añaden campos; no cambia ninguno de
    los que había.
  - **Vistas nuevas** en `--render-snapshot`: `panel-wifi`, `panel-bluetooth` y `panel-morph`.
  - **Pruebas:** 3 casos nuevos para el recorrido del morph, qué responde con una tarjeta
    abierta y la tarjeta con la radio apagada. 33 en total.
  - **Medido en vivo al 125 %:** la ventana pasa de 490 a 616 px con un píxel de rebote, en 29
    fotogramas de 16,1 ms, y cuesta 1,3 ms dibujar cada uno.

- **Fase 5: Wi-Fi y Bluetooth.**
  - **Los dos tiles encienden y apagan su radio de verdad** con `Windows.Devices.Radios`.
    `RequestAccessAsync` se pide una vez; en esta máquina contesta «Allowed», sin preguntar
    nada. Cuando Windows avisa de que una radio cambió, el tile cambia también: lo que se haga
    desde Win+A o desde Configuración aparece en el panel.
  - **Wi-Fi:** debajo dice la red a la que estás conectado, sacada de los perfiles de conexión
    de `NetworkInformation` (`GetConnectedSsid`), aunque esa red no tenga internet. Sin WlanAPI:
    el panel **no** aparece en el icono de «ubicación en uso». Comprobado en el registro de
    permisos de ubicación, donde no hay ninguna entrada suya.
  - **Bluetooth:** debajo dice cuántos dispositivos hay conectados. Lo cuentan dos
    `DeviceWatcher`, uno clásico y otro LE; un dispositivo que habla los dos cuenta una vez, por
    su dirección (`BluetoothAddressFromId`, con pruebas).
  - **Clic derecho en un tile:** abre su página de Configuración (`ms-settings:network-wifi`,
    `bluetooth` o `nightlight`). En el resto del panel, el clic derecho sigue abriendo el menú.
  - **Una radio apagada por un interruptor físico o por el firmware** sale atenuada, y el panel
    no hace como que la puede encender.
  - **Carrera arreglada antes de salir:** `Publish()` se llama a la vez desde el hilo de trabajo
    y desde los avisos de red. Un aviso que había leído el Wi-Fi «encendido» guardaba después
    del «apagado» del hilo de trabajo, y el tile se quedaba encendido; con eso, el segundo clic
    volvía a apagar en vez de encender. Ahora leer y guardar van bajo el mismo mutex.
    Encontrado con la sonda, no leyendo el código.
  - **Auditoría:** la regla 2.6 prohibía `WlanConnect\w*` y saltaba con
    `WlanConnectionProfileDetails`, que solo lee el nombre de la red. Ahora solo prohíbe
    `WlanConnect` y `WlanConnect2`, que son las funciones que conectan.
  - **Compilación:** `/external:anglebrackets /external:W0` para no ver los warnings de las
    cabeceras C++/WinRT del SDK, y se enlaza `windowsapp`.
  - **Pruebas:** 3 casos nuevos para las direcciones Bluetooth, 30 en total.
  - **Probado con las radios de verdad:**
    - Wi-Fi apagado y encendido desde el panel;
    - Bluetooth apagado desde el panel y encendido desde fuera, y el panel lo siguió;
    - las dos radios quedaron encendidas y la red volvió.
  - **Medido:**
    - en calma, 31 ms de CPU en 30 s con el panel escondido, y 2,2 MB;
    - los avisos de red que Windows manda de vez en cuando (hay Tailscale además del Wi-Fi)
      cuestan una lectura del SSID cada uno.

- **Fase 4a: el brillo del portátil.**
  - **La barra del brillo mueve la pantalla del portátil** por WMI (`ROOT\WMI`,
    `WmiMonitorBrightness` y `WmiSetBrightness`). Toda la conversación con WMI va en el hilo de
    trabajo nuevo (`system/worker`, COM en modo MTA), que es el dueño de la única conexión: la
    interfaz no espera nunca a WMI.
  - **Escritura:** mientras arrastras, solo se escribe el último valor pendiente (`Worker::Post`
    con clave).
  - **Sigue las teclas Fn** con `RegisterPowerSettingNotification` y
    `GUID_VIDEO_CURRENT_MONITOR_BRIGHTNESS`: el aviso llega como `WM_POWERBROADCAST` y no hace
    falta ningún evento de WMI. Una suscripción a WMI habría lanzado `unsecapp.exe` o dejado un
    hilo sondeando. Cambia `SEGURIDAD.md` §2.3 en su propio commit, y cierra más de lo que abre.
  - **El eco de nuestras escrituras:** durante 400 ms después de escribir, los avisos de Windows
    se toman como eco y no mueven la barra. Así un aviso atrasado no la hace saltar hacia
    atrás.
  - **Sin pantalla interna** (un sobremesa), la tarjeta dice «Sin control de brillo».
  - **Una tarjeta solo se despliega si hay más de una cosa que elegir** (`CanUnfold`). Con una
    sola pantalla o una sola salida no hay chevron, y su cabecera no es parada del Tab.
  - **Memoria:** el panel recorta su memoria al arrancar y después de la primera lectura de WMI,
    no solo al esconderse. Sin abrirlo nunca bajó de 46,1 MB a 0,4 MB. La primera apertura
    tarda 21 ms y las siguientes 10.
  - **Pruebas:** 2 casos nuevos para el hilo de trabajo (el orden y que los trabajos con clave
    se sustituyen), 27 en total.
  - **Probado con la pantalla del portátil** (AUO, 101 niveles):
    - lee el 100 % y el arrastre la deja al 40 %;
    - un cambio desde fuera al 70 % llega al panel por el aviso de Windows;
    - quedó en el 100 % del principio.

- **Fase 3b: elegir la salida de audio.**
  - La tarjeta del volumen se despliega con las salidas activas de verdad
    (`EnumAudioEndpoints`). Cada una lleva el icono de auriculares o de altavoz según su forma,
    y la marca va en la predeterminada.
  - **Nombres:** si dos salidas comparten el nombre corto, las dos enseñan el largo. En esta
    máquina, las tres «Altavoces» salen como «Altavoces (Realtek(R) Audio)» y las dos de Steam.
    La cabecera usa el mismo nombre que la fila.
  - **Elegir:** un clic en una fila la hace predeterminada con `IPolicyConfig`, en los tres
    roles. Esa API solo aparece en `system/policy_config.h`, y justo antes de llamarla se vuelve
    a enumerar: una salida desenchufada con el panel abierto no se elige.
  - **La lista se mantiene al día:** enchufar o quitar algo la actualiza con el panel abierto. Si
    se queda vacía, la tarjeta se pliega sola.
  - **Si este Windows no tiene `IPolicyConfig`,** la lista se ve apagada y un clic lo explica en
    la línea de avisos. Los avisos se borran al esconder el panel.
  - La enmienda a `SEGURIDAD.md` §2.1 y §2.2 va en su propio commit, antes que el código.
  - **Auditoría:** la regla de WMI saltaba con cualquier texto que dijera «from here». Ahora
    solo mira consultas `SELECT … FROM`. La sonda de violaciones sigue pillando `MSFT_Disk`.
  - **Pruebas:** 3 casos nuevos para los nombres, 25 en total.
  - **Probado desde el propio panel:**
    - un clic en «Altavoces (Realtek(R) Audio)» la hace predeterminada, y la cabecera pasa a
      esa salida y a su nivel (3 %);
    - otro clic vuelve a «Auriculares»;
    - se abre en 9-15 ms listando las salidas;
    - al terminar, el audio del usuario quedó en «Auriculares» al 100 %.

- **Fase 3a: el volumen, de verdad.**
  - El deslizador lee y escribe el volumen maestro de la salida predeterminada con Core Audio.
    El icono del altavoz silencia de verdad, y subir el volumen quita el silencio, como hace el
    HUD.
  - La cabecera dice por dónde sale el sonido con el nombre corto del dispositivo
    («Auriculares»), y el largo solo si el driver no da el corto. La enmienda a
    `SEGURIDAD.md` §2.1 va en su propio commit, antes que el código.
  - **Sigue a la salida:** el aviso de cambio de dispositivo va en el enumerador, y el endpoint
    viejo se suelta antes de volver a usarse, porque no falla nunca y seguiría dando los números
    del dispositivo anterior. Es el arreglo que midieron el HUD y la isla.
  - Los cambios que vienen de fuera (teclas, el HUD, el mezclador de Windows) llegan por aviso,
    sin consultar en bucle. Con el panel escondido no se hace nada: lee una vez al abrirse.
  - Las escrituras del panel llevan su propio GUID de contexto, `kPanelVolumeContext`, y su eco
    se ignora.
  - Si Core Audio falla de verdad, lo reintenta a los 2 s. Un equipo sin ninguna salida no es un
    fallo: espera al aviso de que aparezca una.
  - La lista de salidas de ejemplo se quita de la app hasta la fase 3b, y una tarjeta sin nada
    que desplegar no dibuja el chevron.
  - **Probado contra el sistema, con una sonda aparte:**
    - lee el 100 %, arrastra al 30 %, silencia y quita el silencio;
    - un cambio desde fuera al 55 % aparece en el panel;
    - cambiando la salida a «Altavoces» con el panel abierto, el nombre y el nivel (3 %) pasan
      a los suyos, y al volver quedan «Auriculares» al 100 %;
    - al terminar, el volumen y la salida del usuario quedaron como estaban.

- **Fase 2: los controles, todavía con datos de ejemplo.**
  - **Tiles y utilidades:** hover con un fundido de 120 ms, y al pulsarlos se hunden al 97 %. Si
    sueltas el botón fuera del tile, no pasa nada.
  - **Deslizadores:**
    - un clic lleva la barra a ese punto y se puede arrastrar;
    - la rueda mueve un 2 % por muesca;
    - el porcentaje aparece en la cabecera mientras los tocas;
    - el icono del altavoz silencia, y subir el volumen quita el silencio, como hace el HUD.
  - **Tarjetas desplegables:**
    - van con un muelle de 250 ms de periodo y se pueden interrumpir: un segundo clic a mitad
      da la vuelta desde donde está, sin saltos;
    - el chevron gira con el mismo muelle;
    - la ventana crece hacia arriba, anclada a la esquina;
    - en el brillo, la barra única viaja hasta la fila de su pantalla mientras se funde.
  - **Teclado completo:**
    - `Tab`, flechas, `RePág`/`AvPág`, `Inicio`/`Fin`, `Espacio`/`Enter` y `Esc`;
    - un anillo de foco que solo sale tras usar el teclado, como en Windows.
  - **Reloj de fotogramas:** es el de Agenda (`vsync`). Todo lo que se mueve va con él, y se
    para en cuanto nada se mueve.
  - **Vista `panel-estados`** en `--render-snapshot`.
  - **Pruebas:** 22 casos, 7 de ellos nuevos, para lo que hay bajo el ratón, el orden del foco,
    el valor de un deslizador según la posición y el muelle.
  - **Medido en esta máquina** (una pantalla, al 125 %):
    - desplegar el brillo lleva la ventana de 490 a 668 px en 22 fotogramas de 15,9 ms, a la
      frecuencia del monitor;
    - dibujar cuesta 1,3-1,9 ms por fotograma;
    - escondido, 0 ms de CPU en 20 s.

- **Fase 1: el esqueleto, con datos de ejemplo.**
  - `Ctrl+Alt+A` abre y cierra el panel abajo a la derecha del monitor del ratón: sube 8 DIP
    en 160 ms y se funde en 120, como Agenda.
  - `Esc`, `Alt+F4` y hacer clic fuera lo esconden. El clic derecho abre un menú con «Abrir
    panel.json» y «Salir».
  - El diseño completo está pintado:
    - cuatro tiles (Wi-Fi, Bluetooth, Luz nocturna y Configuración, que dice «Próximamente»);
    - brillo y volumen con deslizadores gruesos, cada uno con su icono dentro;
    - el brillo desplegado, con una barra por pantalla;
    - el volumen desplegado, con las salidas;
    - la fila de utilidades, con un punto verde en las que están en marcha.
  - Temas oscuro, claro y alto contraste, con el color de acento de Windows.
  - `PanelState` es el esquema de todo lo que enseña, completo desde ya, y cada fase solo lo
    rellena. Tiene un campo `notice` para contar errores dentro del panel.
  - Si otra aplicación tiene el atajo, el panel se abre una vez al arrancar y lo dice en esa
    línea.
  - `--render-snapshot` genera las vistas `panel`, `panel-brillo` y `panel-volumen`, con
    `--theme`.
  - Instancia única y `--monitor=N` / `PANEL_DEV_MONITOR`.
  - 15 casos de prueba con doctest: el atajo, las opciones y el layout.
  - **Medido en esta máquina** (una pantalla, al 125 %):
    - se abre en 11 ms;
    - escondido gasta 0,8 MB de memoria privada y unos 31 ms de CPU en 30 s;
    - en Release, el ejecutable ocupa 612 KB.

- **Fase 0: documentos antes del código.**
  - `CLAUDE.md` recoge el stack fijado, la API elegida para cada función, el sistema de
    diseño, las reglas de arquitectura y las fases.
  - `SEGURIDAD.md` dice qué no hará nunca el panel (red, ubicación, administrador, ganchos
    de teclado, matar procesos) y con qué cortes toca el sistema (`IPolicyConfig`, WMI,
    DDC/CI, luz nocturna, radios y utilidades).
  - `auditar.ps1` comprueba 14 reglas. Se probó con código que incumple cada una y con
    código correcto que las pasa.
  - El plan completo está en `docs/superpowers/plans/`.
