# Changelog

Formato [Keep a Changelog](https://keepachangelog.com/es-ES/1.1.0/), versiones SemVer.

## [Unreleased]

### Añadido

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
