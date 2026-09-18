using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Dwm;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Dock;

/// <summary>
/// Las tres únicas cosas que el dock le hace a una ventana ajena: mirar si está al
/// frente, traerla al frente, y minimizarla.
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
