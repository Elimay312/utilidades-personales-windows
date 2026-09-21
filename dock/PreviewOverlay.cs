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

namespace Dock;

/// <summary>
/// La miniatura de la ventana elegida en la lista de la rueda.
///
/// <para>
/// <b>Por qué no con las miniaturas de DWM.</b> <c>DwmRegisterThumbnail</c> es la API de
/// las vistas previas de la barra de tareas, y sería lo obvio — pero no existe ninguna
/// forma de convertir un <c>HTHUMBNAIL</c> en un <c>Visual</c>, una
/// <c>CompositionSurface</c> ni un <c>CompositionBrush</c>: solo se consume dibujándolo
/// directamente contra un HWND. Para un dock hecho entero con el compositor eso
/// significa sin esquinas redondeadas, sin material de fondo y sin poder animarlo. Es un
/// mundo paralelo al árbol que ya tenemos.
/// </para>
///
/// <para>
/// <b>Y por qué no con Windows.Graphics.Capture.</b> Funciona, pero obliga a una sesión
/// persistente con framepool y a un borde amarillo alrededor de la ventana capturada.
/// Quitar el borde exige identidad de paquete y una capability — y quitarle al usuario
/// el aviso de que le están capturando la pantalla es justo lo que querría un espía.
/// </para>
///
/// <para>
/// Así que se usa <see cref="WindowCapture"/>, que ya existía para el efecto genio:
/// <c>PrintWindow</c> con <c>PW_RENDERFULLCONTENT</c>, un fotograma cada vez. La
/// enmienda 3 es la que autoriza repetirlo mientras dura el gesto, y
/// pone el límite: solo mientras la lista está abierta, solo esa ventana, y se para al
/// cerrarla.
/// </para>
///
/// La ventana es <c>WS_EX_TRANSPARENT</c>: el ratón la atraviesa entera. No tiene
/// hit-test, ni clics, ni nada que gestionar — quien manda sigue siendo la lista de
/// debajo.
/// </summary>
internal sealed unsafe class PreviewOverlay : IDisposable
{
    private const string ClassName = "DockPreviewOverlayClass";

    /// <summary>Ancho máximo de la miniatura, en unidades lógicas.</summary>
    private const float LogicalWidth = 300f;

    /// <summary>Alto máximo. Con los dos, la captura entra por el lado que le apriete.</summary>
    private const float LogicalHeight = 190f;

    private const float LogicalPad = 8f;

    private static ushort _classAtom;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly DockVisuals _owner;
    private readonly float _scale;

    private HWND _hwnd;
    private bool _disposed;

    /// <summary>La captura subida al compositor, para poder soltarla al cambiarla.</summary>
    private CompositionSurfaceBrush? _shot;

    private PreviewOverlay(DockVisuals owner, float scale, int x, int y, int w, int h)
    {
        _owner = owner;
        _compositor = owner.Compositor;
        _scale = scale;

        EnsureClassRegistered();

        fixed (char* className = ClassName)
        fixed (char* title = "Preview")
        {
            _hwnd = PInvoke.CreateWindowEx(
                // TRANSPARENT: no recoge ratón. NOACTIVATE: no roba el foco, que es
                // criterio de aceptación del proyecto.
                WINDOW_EX_STYLE.WS_EX_TRANSPARENT
                    | WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(className),
                new PCWSTR(title),
                WINDOW_STYLE.WS_POPUP,
                x, y, w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("no se pudo crear la ventana de la miniatura");

        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(_hwnd, true, out _target);

        _root = _compositor.CreateContainerVisual();
        _root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _root;
    }

    private static HINSTANCE ModuleHandle => (HINSTANCE)(nint)PInvoke.GetModuleHandle((PCWSTR)null);

    /// <summary>
    /// Enseña la captura encima del punto dado, centrada en el icono y sin salirse del
    /// monitor. <paramref name="bottom"/> es por dónde tiene que acabar: justo encima de
    /// la lista de ventanas.
    /// </summary>
    public static PreviewOverlay? Show(
        DockVisuals owner, IconBitmap shot, float scale, float anchorX, int bottom, RECT monitor)
    {
        (int w, int h, int x, int y) = Layout(shot, scale, anchorX, bottom, monitor);

        try
        {
            PreviewOverlay overlay = new(owner, scale, x, y, w, h);
            overlay.Build(shot, new Vector2(w, h));
            PInvoke.ShowWindow(overlay._hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
            return overlay;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[preview] no se pudo abrir: {ex.Message}");
            return null;
        }
    }

    /// <summary>
    /// Cambia la imagen sin recrear la ventana. Es lo que usa tanto el refresco como
    /// pasar a otra ventana de la lista: recrear la ventana en cada muesca daría un
    /// parpadeo.
    /// </summary>
    public void Update(IconBitmap shot, float anchorX, int bottom, RECT monitor)
    {
        if (_disposed) return;

        (int w, int h, int x, int y) = Layout(shot, _scale, anchorX, bottom, monitor);
        PInvoke.SetWindowPos(_hwnd, default, x, y, w, h,
            SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER);

        _root.Children.RemoveAll();
        SueltaCaptura();
        Build(shot, new Vector2(w, h));
    }

    /// <summary>
    /// Qué tamaño y sitio le toca. La captura se encaja en el rectángulo máximo
    /// conservando su proporción: una ventana apaisada y una vertical no pueden ocupar
    /// la misma caja sin deformarse, y una miniatura deformada no se reconoce.
    /// </summary>
    private static (int W, int H, int X, int Y) Layout(
        IconBitmap shot, float scale, float anchorX, int bottom, RECT monitor)
    {
        float pad = LogicalPad * scale;
        float maxW = LogicalWidth * scale;
        float maxH = LogicalHeight * scale;

        float fit = Math.Min(maxW / shot.Width, maxH / shot.Height);
        float imageW = MathF.Max(16f, shot.Width * fit);
        float imageH = MathF.Max(16f, shot.Height * fit);

        int w = (int)MathF.Ceiling(imageW + pad * 2f);
        int h = (int)MathF.Ceiling(imageH + pad * 2f);

        int x = (int)Math.Clamp(anchorX - w * 0.5f, monitor.left + 8, Math.Max(monitor.left + 8, monitor.right - w - 8));
        int y = Math.Max(monitor.top + 8, bottom - h - (int)(6f * scale));

        return (w, h, x, y);
    }

    private void Build(IconBitmap shot, Vector2 size)
    {
        float pad = LogicalPad * _scale;

        // Mismo material que la barra y que la rejilla de carpetas: es una pieza más del
        // dock, no una ventana con su propio aspecto.
        ContainerVisual chip = _compositor.CreateContainerVisual();
        chip.RelativeSizeAdjustment = Vector2.One;

        CompositionRoundedRectangleGeometry round = _compositor.CreateRoundedRectangleGeometry();
        round.Size = size;
        round.CornerRadius = new Vector2(12f * _scale);
        chip.Clip = _compositor.CreateGeometricClip(round);

        SpriteVisual material = _compositor.CreateSpriteVisual();
        material.RelativeSizeAdjustment = Vector2.One;
        material.Brush = _owner.CreateBackdropBrush();
        chip.Children.InsertAtBottom(material);

        SpriteVisual tint = _compositor.CreateSpriteVisual();
        tint.RelativeSizeAdjustment = Vector2.One;
        tint.Brush = _compositor.CreateColorBrush(Color.FromArgb(52, 255, 255, 255));
        chip.Children.InsertAtTop(tint);

        _root.Children.InsertAtBottom(chip);

        SpriteVisual image = _compositor.CreateSpriteVisual();
        image.Size = new Vector2(size.X - pad * 2f, size.Y - pad * 2f);
        image.Offset = new Vector3(pad, pad, 0f);
        // El tamaño del chip, no el de la ventana: una captura de 1920x1080 subida
        // entera son 8 MB por refresco, y aquí hay dos por segundo.
        _shot = _owner.CreateBitmapBrush(shot, MathF.Max(image.Size.X, image.Size.Y));
        image.Brush = _shot;

        CompositionRoundedRectangleGeometry imageRound = _compositor.CreateRoundedRectangleGeometry();
        imageRound.Size = image.Size;
        imageRound.CornerRadius = new Vector2(7f * _scale);
        image.Clip = _compositor.CreateGeometricClip(imageRound);

        _root.Children.InsertAtTop(image);
    }

    private static void EnsureClassRegistered()
    {
        if (_classAtom != 0) return;

        fixed (char* className = ClassName)
        {
            WNDCLASSEXW wc = new()
            {
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
                lpfnWndProc = PInvoke.DefWindowProc,
                hInstance = ModuleHandle,
                lpszClassName = new PCWSTR(className),
                hbrBackground = default,
            };

            _classAtom = PInvoke.RegisterClassEx(in wc);
        }
    }

    /// <summary>
    /// Suelta la captura subida. La superficie es dueña de los píxeles, así que va
    /// también: mismo patrón que <see cref="GenieOverlay"/>, donde no soltarla costó
    /// +5 MB por animación. Aquí se refresca dos veces por segundo, así que sin esto
    /// cada miniatura queda esperando al finalizador.
    /// </summary>
    private void SueltaCaptura()
    {
        (_shot?.Surface as CompositionDrawingSurface)?.Dispose();
        _shot?.Dispose();
        _shot = null;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        _target.Root = null;
        _root.Dispose();
        _target.Dispose();
        SueltaCaptura();

        if (!_hwnd.IsNull)
        {
            PInvoke.DestroyWindow(_hwnd);
            _hwnd = default;
        }
    }
}
