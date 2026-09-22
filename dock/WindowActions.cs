using System.Drawing;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Dwm;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Dock;

/// <summary>
/// Las únicas cosas que el dock le hace a una ventana ajena: mirar si está al frente
/// o a la vista, traerla al frente, y minimizarla.
///
/// Todas están sujetas a la enmienda 1 de SEGURIDAD.md: solo se llaman como respuesta
/// directa a un clic del usuario sobre un icono del dock, nunca desde un temporizador
/// ni desde un hilo de fondo. Es exactamente lo que hace la barra de tareas al pulsar
/// su botón.
/// </summary>
internal static unsafe class WindowActions
{
    public static bool IsForeground(HWND window)
        => !window.IsNull && PInvoke.GetForegroundWindow() == window;

    public static bool IsMinimized(HWND window)
        => !window.IsNull && PInvoke.IsIconic(window);

    /// <summary>
    /// ¿Está la ventana <b>a la vista</b>, o solo abierta? Para el usuario, minimizada y
    /// tapada por otra son el mismo caso: no la ve. A la vista es lo otro.
    ///
    /// Se pregunta por el punto central porque la respuesta ya la tiene el sistema:
    /// <c>WindowFromPoint</c> devuelve la ventana que está más arriba en ese píxel, así
    /// que si la de más arriba es la nuestra, ahí se ve. Recorrer el orden Z a mano da lo
    /// mismo con veinte líneas más y con el mismo margen de error.
    ///
    /// Los dos sesgos conocidos caen del lado seguro — decir «no se ve» y sacarla, antes
    /// que tragarse algo que el usuario estaba mirando:
    /// <list type="bullet">
    /// <item>Si el centro cae debajo del propio dock, contesta el dock: WindowFromPoint
    /// dice quién está encima, no quién recoge el clic, y por eso ignora HTTRANSPARENT.</item>
    /// <item>Una ventana tapada justo por el centro cuenta como tapada aunque se le vea
    /// un borde.</item>
    /// </list>
    /// </summary>
    public static bool IsOnScreen(HWND window)
    {
        if (window.IsNull || PInvoke.IsIconic(window)) return false;
        if (!PInvoke.GetWindowRect(window, out RECT rect)) return false;

        Point center = new((rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
        HWND top = PInvoke.WindowFromPoint(center);

        // GA_ROOT porque en ese píxel lo que hay es un control hijo, no la ventana.
        return !top.IsNull && PInvoke.GetAncestor(top, GET_ANCESTOR_FLAGS.GA_ROOT) == window;
    }

    /// <summary>
    /// Trae la ventana al frente, restaurándola si estaba minimizada.
    ///
    /// Windows IGNORA SetForegroundWindow salvo que quien la llame cumpla alguna de sus
    /// condiciones, y la que nos vale es <b>haber recibido el último evento de entrada</b>:
    /// o sea, el clic del usuario sobre el icono. Sin ese clic la llamada no hace nada,
    /// lo cual es el propio sistema operativo imponiendo la regla 11 de SEGURIDAD.md por
    /// nosotros.
    ///
    /// (Comprobado por accidente: una primera versión de la prueba mandaba el clic con
    /// PostMessage en vez de pinchar de verdad, y el foco no cambiaba nunca.)
    ///
    /// El baile de AttachThreadInput parece sucio y no lo es: adjuntar temporalmente
    /// nuestra cola de entrada a la del hilo dueño de la ventana es el rodeo documentado
    /// para los casos en que la condición anterior no basta. Se desadjunta siempre.
    /// </summary>
    public static void BringToFront(HWND window, bool instant = false)
    {
        if (window.IsNull) return;

        if (PInvoke.IsIconic(window))
        {
            // Igual que al minimizar: si el genio ya ha hecho la animación, la de
            // Windows sobra. Ver Minimize.
            uint off = 1;
            if (instant) PInvoke.DwmSetWindowAttribute(window, DWMWINDOWATTRIBUTE.DWMWA_TRANSITIONS_FORCEDISABLED, &off, 4);

            PInvoke.ShowWindow(window, SHOW_WINDOW_CMD.SW_RESTORE);

            uint on = 0;
            if (instant) PInvoke.DwmSetWindowAttribute(window, DWMWINDOWATTRIBUTE.DWMWA_TRANSITIONS_FORCEDISABLED, &on, 4);
        }

        uint target = PInvoke.GetWindowThreadProcessId(window, null);
        uint self = PInvoke.GetCurrentThreadId();

        if (target == self || target == 0)
        {
            PInvoke.SetForegroundWindow(window);
            return;
        }

        // AttachThreadInput puede fallar y no pasa nada: con las apps UWP falla, porque
        // su hilo vive en otro contenedor, y aun así SetForegroundWindow funciona. El
        // permiso de verdad viene de haber recibido el clic, no del adjuntado.
        bool attached = PInvoke.AttachThreadInput(self, target, true);
        try
        {
            PInvoke.SetForegroundWindow(window);
        }
        finally
        {
            if (attached) PInvoke.AttachThreadInput(self, target, false);
        }
    }

    /// <summary>
    /// Sube la ventana al frente <b>sin activarla</b>, y la restaura si estaba
    /// minimizada.
    ///
    /// Existe aparte de <see cref="BringToFront"/> por una razón medida: Windows no
    /// concede el derecho a cambiar la ventana en primer plano por haber recibido una
    /// rueda. Se comprobó lado a lado, con las mismas ventanas y el mismo icono: con un
    /// clic la ventana pasa al frente; con la rueda, SetForegroundWindow no surte
    /// efecto ni una sola vez. Así que el gesto de la rueda se queda en subirla en el
    /// orden de apilado, que no necesita permiso ninguno.
    ///
    /// La consecuencia, y hay que saberla: la ventana se ve, pero el teclado sigue
    /// donde estaba. Un clic encima la activa como siempre.
    /// </summary>
    public static void Raise(HWND window)
    {
        if (window.IsNull) return;

        if (PInvoke.IsIconic(window)) PInvoke.ShowWindow(window, SHOW_WINDOW_CMD.SW_RESTORE);

        PInvoke.SetWindowPos(window, HWND.Null, 0, 0, 0, 0,
            SET_WINDOW_POS_FLAGS.SWP_NOMOVE | SET_WINDOW_POS_FLAGS.SWP_NOSIZE
                | SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE | SET_WINDOW_POS_FLAGS.SWP_SHOWWINDOW);
    }

    /// <summary>
    /// Minimiza. Con <paramref name="instant"/> la ventana desaparece de golpe, sin la
    /// animación que Windows le pone.
    ///
    /// <para>
    /// No hay forma documentada de callar esa animación en una ventana ajena, así que se
    /// midió: con DWMWA_TRANSITIONS_FORCEDISABLED puesto, a los 25 ms ya había ocurrido
    /// el 30% del cambio; sin él, el 0%. O sea que surte efecto — ver el apéndice de
    /// SEGURIDAD.md, donde también se justifica por qué es benigno. Se vuelve a dejar
    /// como estaba en cuanto ShowWindow retorna: DWM ya decidió durante la llamada, y
    /// dejar las transiciones de otra app apagadas para siempre sería una grosería.
    /// </para>
    ///
    /// Nunca se toca SPI_SETANIMATION: eso es un ajuste global del sistema.
    /// </summary>
    public static void Minimize(HWND window, bool instant = false)
    {
        if (window.IsNull) return;

        uint off = 1;
        if (instant) PInvoke.DwmSetWindowAttribute(window, DWMWINDOWATTRIBUTE.DWMWA_TRANSITIONS_FORCEDISABLED, &off, 4);

        PInvoke.ShowWindow(window, SHOW_WINDOW_CMD.SW_MINIMIZE);

        uint on = 0;
        if (instant) PInvoke.DwmSetWindowAttribute(window, DWMWINDOWATTRIBUTE.DWMWA_TRANSITIONS_FORCEDISABLED, &on, 4);
    }
}
