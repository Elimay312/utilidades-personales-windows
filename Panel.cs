using System.Numerics;
using System.Runtime.InteropServices;
using Windows.UI;
using Windows.UI.Composition;
using Windows.UI.Composition.Desktop;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.System.WinRT.Composition;
using Windows.Win32.UI.WindowsAndMessaging;
using WinRT;

namespace QuickLook;

/// <summary>
/// El panel de la vista previa.
///
/// <para>
/// <b>Nunca roba el foco.</b> <c>WS_EX_NOACTIVATE</c> no basta: hay que contestar
/// <c>MA_NOACTIVATE</c> a <c>WM_MOUSEACTIVATE</c>, o el primer clic activa la ventana. Y
/// aqui no es solo estetica: si el panel robase el foco, el Explorador perderia el
/// resaltado de la seleccion y el segundo espacio no llegaria por el mismo camino que el
/// primero.
/// </para>
///
/// <para>
/// <b>La ventana es siempre la caja maxima; lo que cambia de tamano es la tarjeta de
/// dentro.</b> Es la decision que hace posible el morph. Si la ventana se ajustase a cada
/// contenido, cambiar de archivo obligaria a un <c>SetWindowPos</c> por fotograma desde
/// nuestro hilo, y una animacion movida a mano desde el hilo de UI se atasca en cuanto el
/// shell tarda en devolver una miniatura. Asi no se mueve ninguna ventana: la tarjeta
/// crece, encoge y se recoloca dentro, y eso lo lleva el hilo de DWM.
/// </para>
///
/// <para>
/// El precio es que la ventana recoge clics en toda la caja maxima, tambien fuera de la
/// tarjeta. No es un problema: un clic ahi cierra el panel, que es justo lo que hace Quick
/// Look en macOS al clicar fuera.
/// </para>
/// </summary>
internal sealed unsafe class Panel : IDisposable
{
    private const string ClassName = "QuickLookPanelClass";

    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const uint WM_LBUTTONDOWN = 0x0201;
    private const uint WM_RBUTTONDOWN = 0x0204;
    private const int MA_NOACTIVATE = 3;

    /// <summary>IDC_ARROW. CsWin32 no proyecta los cursores del sistema como constante.</summary>
    private const int IdcArrow = 32512;

    /// <summary>Donde avisar de que hay que cerrar. Lo pone HostWindow al arrancar.</summary>
    internal static HWND Host;

    /// <summary>Tamano maximo de la tarjeta, en fraccion del area de trabajo.</summary>
    private const float MaxWidth = 0.62f;
    private const float MaxHeight = 0.72f;

    /// <summary>Margen alrededor del contenido, en unidades logicas.</summary>
    private const float Pad = 16f;

    /// <summary>Alto del pie con el nombre y los datos.</summary>
    private const float CaptionHeight = 46f;

    /// <summary>La ficha no depende del contenido, asi que tiene tamano fijo.</summary>
    private const float CardWidth = 460f;
    private const float CardHeight = 300f;

    /// <summary>Lado del boton de cerrar, en unidades logicas.</summary>
    private const float CloseSize = 26f;

    private static readonly WNDPROC WndProcThunk = WndProc;
    private static readonly Dictionary<nint, Panel> Instances = [];

    private static ushort _classAtom;

    /// <summary>QL_LOG=1 para ver el tamano de la tarjeta en cada cambio.</summary>
    private static readonly bool Trace = Environment.GetEnvironmentVariable("QL_LOG") == "1";

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly float _scale;

    /// <summary>El area de trabajo del monitor, para poder recalcular la tarjeta al morfar.</summary>
    private readonly int _availableW;
    private readonly int _availableH;

    private readonly Vector2 _window;

    /// <summary>La tarjeta: el material, las esquinas y el contenido. Es lo que morfa.</summary>
    private ContainerVisual _card = null!;

    /// <summary>Las esquinas de la tarjeta. Su tamano se anima junto con el de la tarjeta.</summary>
    private CompositionRoundedRectangleGeometry _round = null!;

    /// <summary>Lo de dentro. Al cambiar de archivo se cruza con el nuevo y se tira.</summary>
    private ContainerVisual? _content;

    private HWND _hwnd;
    private bool _disposed;
    private bool _closing;

    private Panel(int x, int y, int w, int h, float scale, int availableW, int availableH)
    {
        _compositor = Visuals.Compositor;
        _scale = scale;
        _window = new Vector2(w, h);
        _availableW = availableW;
        _availableH = availableH;

        EnsureClassRegistered();

        fixed (char* className = ClassName)
        fixed (char* title = "QuickLook")
        {
            _hwnd = PInvoke.CreateWindowEx(
                WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(className),
                new PCWSTR(title),
                WINDOW_STYLE.WS_POPUP,
                x, y, w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("no se pudo crear el panel");
        Instances[(nint)_hwnd.Value] = this;

        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(_hwnd, true, out _target);

        _root = _compositor.CreateContainerVisual();
        _root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _root;
    }

    private static HINSTANCE ModuleHandle => (HINSTANCE)(nint)PInvoke.GetModuleHandle((PCWSTR)null);

    /// <summary>Su ventana, para que la host pueda saber si sigue siendo la de delante.</summary>
    public HWND Handle => _hwnd;

    /// <summary>Que archivo se esta ensenando. Lo compara el temporizador de la host.</summary>
    public string Path { get; private set; } = "";

    /// <summary>
    /// Abre el panel en el monitor de <paramref name="near"/>, que es la ventana del
    /// Explorador desde la que se pulso el espacio. Devuelve null si algo falla: un fallo
    /// aqui no puede tirar el programa, porque el hook seguiria comiendose la barra
    /// espaciadora del usuario.
    /// </summary>
    public static Panel? Open(HWND near, string path, Preview preview)
    {
        Panel? panel = null;
        try
        {
            (int x, int y, int w, int h, float scale, int aw, int ah) = Layout(near);
            panel = new Panel(x, y, w, h, scale, aw, ah) { Path = path };
            panel.Build(preview);

            // El estado de partida se deja puesto ANTES de ensenar la ventana. Si se
            // ensenara primero habria un fotograma con el panel ya a tamano completo y
            // opaco, y ese parpadeo se ve.
            panel._root.Opacity = 0f;
            panel._root.Scale = new Vector3(Motion.OpenScale, Motion.OpenScale, 1f);
            panel._root.CenterPoint = Birth(x, y, w, h);

            PInvoke.ShowWindow(panel._hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
            Motion.Open(panel._compositor, panel._root);
            return panel;
        }
        catch (Exception ex)
        {
            // La ventana se crea ANTES que su contenido, asi que si Build falla el HWND ya
            // existe. Sin este Dispose se quedaba una ventana huerfana, sin nada dibujado y
            // sin nadie que la cerrara.
            panel?.Dispose();

            Console.WriteLine($"[panel] no se pudo abrir: {ex.Message}");
            if (Trace) Console.WriteLine(ex);
            return null;
        }
    }

    /// <summary>
    /// Cambia de archivo sin cerrarse: la tarjeta morfa a la forma del contenido nuevo y lo
    /// de dentro se cruza. Es el gesto que hace que esto se sienta como Quick Look y no
    /// como abrir y cerrar una ventana dos veces.
    /// </summary>
    public void Morph(string path, Preview preview)
    {
        if (_disposed || _closing || _content is null) return;

        try
        {
            Path = path;

            ContainerVisual old = _content;
            Vector2 size = CardSize(preview);

            _content = BuildContent(preview, size);
            _content.Opacity = 0f;
            _content.Scale = new Vector3(0.96f, 0.96f, 1f);
            _card.Children.InsertAtTop(_content);

            Motion.Morph(
                _compositor, _card, _round, size, CardOffset(size), Radius(size), old, _content,
                () => _card.Children.Remove(old));
        }
        catch (Exception ex)
        {
            // Si el morph falla, el panel se queda con lo que ya tenia dentro. Es mejor que
            // cerrarse en la cara del usuario por un archivo que el shell no supo dibujar.
            Console.WriteLine($"[panel] no se pudo cambiar de archivo: {ex.Message}");
        }
    }

    /// <summary>
    /// De que punto nace el morph: el cursor, porque acabas de clicar el archivo y el raton
    /// esta justo encima. En coordenadas locales y recortado a la ventana, asi que el panel
    /// crece SALIENDO de por donde esta el cursor.
    /// </summary>
    private static Vector3 Birth(int x, int y, int w, int h)
    {
        if (!PInvoke.GetCursorPos(out System.Drawing.Point cursor))
            return new Vector3(w * 0.5f, h * 0.5f, 0f);

        return new Vector3(
            Math.Clamp(cursor.X - x, 0, w),
            Math.Clamp(cursor.Y - y, 0, h),
            0f);
    }

    /// <summary>
    /// La ventana: siempre la caja maxima del monitor donde esta mirando el usuario. No
    /// depende del contenido, asi que no se mueve nunca mientras el panel vive.
    /// </summary>
    private static (int X, int Y, int W, int H, float Scale, int AvailableW, int AvailableH) Layout(HWND near)
    {
        HMONITOR monitor = PInvoke.MonitorFromWindow(near, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY);

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        PInvoke.GetMonitorInfo(monitor, ref info);

        RECT work = info.rcWork;
        int availableW = work.right - work.left;
        int availableH = work.bottom - work.top;

        int w = Math.Max(1, (int)(availableW * MaxWidth));
        int h = Math.Max(1, (int)(availableH * MaxHeight));

        // El DPI sale de la ventana ajena a proposito: es la pantalla donde esta mirando el
        // usuario, y con monitores a escalas distintas no coincide con la nuestra hasta que
        // la ventana ya esta creada ahi.
        float scale = PInvoke.GetDpiForWindow(near) / 96f;
        if (scale <= 0f) scale = 1f;

        return (work.left + (availableW - w) / 2, work.top + (availableH - h) / 2, w, h, scale, availableW, availableH);
    }

    /// <summary>
    /// El tamano de la tarjeta. Una miniatura manda sobre la caja y se encaja conservando su
    /// proporcion: una foto apaisada y una vertical no caben en la misma caja sin
    /// deformarse, y una miniatura deformada no se reconoce. Una ficha no depende del
    /// contenido, asi que es fija.
    ///
    /// Logica pura, y con un caso feo detras: en un monitor pequeno el maximo puede quedar
    /// por debajo de la ficha fija, asi que el resultado se recorta al area disponible
    /// SIEMPRE, tambien para la ficha. Lo comprueba <c>--check</c>.
    /// </summary>
    internal static (int W, int H) Size(Preview preview, float scale, int availableW, int availableH)
    {
        int maxW = Math.Max(1, (int)(availableW * MaxWidth));
        int maxH = Math.Max(1, (int)(availableH * MaxHeight));

        float pad = Pad * scale;
        float caption = CaptionHeight * scale;

        if (preview.IsThumbnail && preview.Image is Pixels image)
        {
            float boxW = MathF.Max(1f, maxW - pad * 2f);
            float boxH = MathF.Max(1f, maxH - pad * 2f - caption);

            float fit = MathF.Min(boxW / image.Width, boxH / image.Height);

            // Nunca se agranda por encima del tamano nativo: estirar una miniatura solo
            // ensena los pixeles mas grandes.
            fit = MathF.Min(fit, 1f);

            int tw = (int)MathF.Ceiling(image.Width * fit + pad * 2f);
            int th = (int)MathF.Ceiling(image.Height * fit + pad * 2f + caption);
            return (Math.Clamp(tw, 1, maxW), Math.Clamp(th, 1, maxH));
        }

        return (Math.Clamp((int)(CardWidth * scale), 1, maxW),
                Math.Clamp((int)(CardHeight * scale), 1, maxH));
    }

    /// <summary>
    /// El tamano de la tarjeta para este contenido, en esta pantalla.
    ///
    /// Se traza con QL_LOG porque desde fuera ya no se puede medir: la VENTANA es siempre
    /// la caja maxima, asi que un GetWindowRect no dice nada de la tarjeta. Es la senal de
    /// texto que sustituye a mirar pixeles.
    /// </summary>
    private Vector2 CardSize(Preview preview)
    {
        (int w, int h) = Size(preview, _scale, _availableW, _availableH);
        if (Trace) Console.WriteLine($"[tarjeta] {w}x{h}  ({(preview.IsThumbnail ? "miniatura" : "ficha")})");
        return new Vector2(w, h);
    }

    /// <summary>Centrada en la ventana.</summary>
    private Vector3 CardOffset(Vector2 size) =>
        new((_window.X - size.X) * 0.5f, (_window.Y - size.Y) * 0.5f, 0f);

    /// <summary>
    /// El radio de las esquinas crece un poco con la tarjeta: el radio que le queda bien a
    /// una ficha pequena se ve apretado en un panorama, y al reves se ve blando.
    /// </summary>
    private Vector2 Radius(Vector2 size) =>
        new(Math.Clamp(MathF.Min(size.X, size.Y) * 0.035f, 10f * _scale, 20f * _scale));

    /// <summary>
    /// La tarjeta: el material acrilico del dock y de la isla, para que las tres utilidades
    /// se vean de la misma familia.
    /// </summary>
    private void Build(Preview preview)
    {
        Vector2 size = CardSize(preview);

        _card = _compositor.CreateContainerVisual();
        _card.Size = size;
        _card.Offset = CardOffset(size);

        _round = _compositor.CreateRoundedRectangleGeometry();
        _round.Size = size;
        _round.CornerRadius = Radius(size);
        _card.Clip = _compositor.CreateGeometricClip(_round);

        SpriteVisual material = _compositor.CreateSpriteVisual();
        material.RelativeSizeAdjustment = Vector2.One;
        material.Brush = Visuals.CreateAcrylicBrush();
        _card.Children.InsertAtBottom(material);

        SpriteVisual tint = _compositor.CreateSpriteVisual();
        tint.RelativeSizeAdjustment = Vector2.One;
        tint.Brush = _compositor.CreateColorBrush(Color.FromArgb(52, 255, 255, 255));
        _card.Children.InsertAtTop(tint);

        _content = BuildContent(preview, size);
        _card.Children.InsertAtTop(_content);

        _root.Children.InsertAtBottom(_card);
    }

    /// <summary>La imagen, el pie y la cruz de cerrar, en un contenedor que se cruza entero.</summary>
    private ContainerVisual BuildContent(Preview preview, Vector2 size)
    {
        ContainerVisual content = _compositor.CreateContainerVisual();
        content.Size = size;

        // Para que el cruce escale desde el centro y no desde la esquina.
        content.CenterPoint = new Vector3(size.X * 0.5f, size.Y * 0.5f, 0f);

        float pad = Pad * _scale;
        float caption = CaptionHeight * _scale;

        if (preview.Image is Pixels image)
        {
            SpriteVisual picture = _compositor.CreateSpriteVisual();
            picture.Brush = Visuals.CreateBitmapBrush(image);

            if (preview.IsThumbnail)
            {
                picture.Size = new Vector2(size.X - pad * 2f, size.Y - pad * 2f - caption);
                picture.Offset = new Vector3(pad, pad, 0f);

                // Esquinas mas cerradas que las de la tarjeta: una esquina interior con el
                // mismo radio que la exterior se ve mas suelta de lo que esta.
                CompositionRoundedRectangleGeometry inner = _compositor.CreateRoundedRectangleGeometry();
                inner.Size = picture.Size;
                inner.CornerRadius = new Vector2(8f * _scale);
                picture.Clip = _compositor.CreateGeometricClip(inner);
            }
            else
            {
                // El icono se queda a su tamano y centrado sobre el pie.
                picture.Size = new Vector2(image.Width, image.Height);
                picture.Offset = new Vector3(
                    (size.X - image.Width) * 0.5f,
                    (size.Y - caption - image.Height) * 0.5f,
                    0f);
            }

            content.Children.InsertAtTop(picture);
        }

        Vector2 captionSize = new(size.X - pad * 2f, caption);
        SpriteVisual pie = _compositor.CreateSpriteVisual();
        pie.Size = captionSize;
        pie.Offset = new Vector3(pad, size.Y - caption - pad * 0.4f, 0f);
        pie.Brush = Visuals.CreateCaptionBrush(preview.Title, preview.Detail, captionSize, _scale);
        content.Children.InsertAtTop(pie);

        content.Children.InsertAtTop(BuildClose());
        return content;
    }

    /// <summary>
    /// La cruz de cerrar, arriba a la izquierda como en macOS.
    ///
    /// No es un boton: el panel entero se cierra al clicarlo. Esta aqui porque hacia falta
    /// que se VIERA que se puede cerrar — sin ella la unica salida era volver al Explorador
    /// y pulsar espacio otra vez, y eso no se adivina.
    /// </summary>
    private ContainerVisual BuildClose()
    {
        float side = CloseSize * _scale;
        float margin = 12f * _scale;

        ContainerVisual button = _compositor.CreateContainerVisual();
        button.Size = new Vector2(side, side);
        button.Offset = new Vector3(margin, margin, 0f);

        CompositionRoundedRectangleGeometry circle = _compositor.CreateRoundedRectangleGeometry();
        circle.Size = button.Size;
        circle.CornerRadius = new Vector2(side * 0.5f);
        button.Clip = _compositor.CreateGeometricClip(circle);

        SpriteVisual disc = _compositor.CreateSpriteVisual();
        disc.RelativeSizeAdjustment = Vector2.One;
        disc.Brush = _compositor.CreateColorBrush(Color.FromArgb(70, 0, 0, 0));
        button.Children.InsertAtBottom(disc);

        // Dos barras cruzadas: la misma receta que la zona de soltar del dock.
        float thick = MathF.Max(1.5f, side * 0.075f);
        float arm = side * 0.40f;
        CompositionColorBrush ink = _compositor.CreateColorBrush(Color.FromArgb(220, 255, 255, 255));

        foreach (float angle in (float[])[45f, -45f])
        {
            SpriteVisual bar = _compositor.CreateSpriteVisual();
            bar.Size = new Vector2(arm, thick);
            bar.Offset = new Vector3((side - arm) * 0.5f, (side - thick) * 0.5f, 0f);
            bar.Brush = ink;
            bar.CenterPoint = new Vector3(arm * 0.5f, thick * 0.5f, 0f);
            bar.RotationAngleInDegrees = angle;
            button.Children.InsertAtTop(bar);
        }

        return button;
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            // Se puede clicar sin que el Explorador pierda el foco.
            case WM_MOUSEACTIVATE:
                return new LRESULT(MA_NOACTIVATE);

            // Un clic en cualquier parte lo cierra, tambien fuera de la tarjeta: es lo que
            // hace Quick Look en macOS al clicar fuera. Se avisa a la ventana-host en vez
            // de cerrarse aqui: es ella la duena del panel y la que lleva el temporizador,
            // y destruir la ventana desde dentro de su propio WndProc es la clase de cosa
            // que revienta tres mensajes despues.
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
                PInvoke.PostMessage(Host, HostWindow.WM_APP_QUICKLOOK, default, default);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    private static void EnsureClassRegistered()
    {
        if (_classAtom != 0) return;

        fixed (char* className = ClassName)
        {
            WNDCLASSEXW wc = new()
            {
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
                lpfnWndProc = WndProcThunk,
                hInstance = ModuleHandle,
                lpszClassName = new PCWSTR(className),
                hCursor = PInvoke.LoadCursor(default, new PCWSTR((char*)IdcArrow)),
            };

            _classAtom = PInvoke.RegisterClassEx(in wc);
        }
    }

    /// <summary>
    /// Se encoge y se va, y solo entonces se destruye. Mientras dura, el panel ya no
    /// responde a nada: quien lo cerro lo ha soltado, y un segundo espacio tiene que abrir
    /// uno nuevo en vez de reanimar este.
    /// </summary>
    public void CloseAnimated()
    {
        if (_disposed || _closing) return;
        _closing = true;

        // Deja de recoger clics en cuanto empieza a irse: 180 ms son de sobra para que un
        // clic rapido cayera en una ventana que ya esta muerta.
        Instances.Remove((nint)_hwnd.Value);

        Motion.Close(_compositor, _root, Dispose);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        _target.Root = null;
        _root.Dispose();
        _target.Dispose();

        if (!_hwnd.IsNull)
        {
            Instances.Remove((nint)_hwnd.Value);
            PInvoke.DestroyWindow(_hwnd);
            _hwnd = default;
        }
    }
}
