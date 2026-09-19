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
/// <c>MA_NOACTIVATE</c> a <c>WM_MOUSEACTIVATE</c>, o el primer clic activa la ventana.
/// Y aqui no es solo estetica: si el panel robase el foco, el Explorador perderia el
/// resaltado de la seleccion y el segundo espacio no llegaria por el mismo camino que
/// el primero.
/// </para>
///
/// <para>
/// Recibe raton —no lleva <c>WS_EX_TRANSPARENT</c>— porque la rueda pasara paginas de PDF
/// y desplazara el texto. El raton llega a la ventana que esta debajo del cursor sin
/// necesidad de foco, asi que las dos cosas conviven.
/// </para>
/// </summary>
internal sealed unsafe class Panel : IDisposable
{
    private const string ClassName = "QuickLookPanelClass";

    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const int MA_NOACTIVATE = 3;

    /// <summary>IDC_ARROW. CsWin32 no proyecta los cursores del sistema como constante.</summary>
    private const int IdcArrow = 32512;

    /// <summary>Tamano maximo del panel, en fraccion del monitor.</summary>
    private const float MaxWidth = 0.62f;
    private const float MaxHeight = 0.72f;

    private static readonly WNDPROC WndProcThunk = WndProc;
    private static readonly Dictionary<nint, Panel> Instances = [];

    private static ushort _classAtom;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly float _scale;

    private HWND _hwnd;
    private bool _disposed;

    private Panel(int x, int y, int w, int h, float scale)
    {
        _compositor = Visuals.Compositor;
        _scale = scale;

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

    /// <summary>
    /// Abre el panel centrado en el monitor de <paramref name="near"/>, que es la ventana
    /// del Explorador desde la que se pulso el espacio. Devuelve null si algo falla: un
    /// fallo aqui no puede tirar el programa, porque el hook seguiria comiendose la barra
    /// espaciadora del usuario.
    /// </summary>
    public static Panel? Open(HWND near)
    {
        try
        {
            (int x, int y, int w, int h, float scale) = Layout(near);
            Panel panel = new(x, y, w, h, scale);
            panel.Build(new Vector2(w, h));
            PInvoke.ShowWindow(panel._hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
            return panel;
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[panel] no se pudo abrir: {ex.Message}");
            return null;
        }
    }

    /// <summary>Que tamano y sitio le toca, en pixeles fisicos del monitor de al lado.</summary>
    private static (int X, int Y, int W, int H, float Scale) Layout(HWND near)
    {
        HMONITOR monitor = PInvoke.MonitorFromWindow(near, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY);

        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        PInvoke.GetMonitorInfo(monitor, ref info);

        RECT work = info.rcWork;
        int availableW = work.right - work.left;
        int availableH = work.bottom - work.top;

        int w = (int)(availableW * MaxWidth);
        int h = (int)(availableH * MaxHeight);

        int x = work.left + (availableW - w) / 2;
        int y = work.top + (availableH - h) / 2;

        // El DPI sale de la ventana ajena a proposito: es la pantalla donde esta
        // mirando el usuario, y con monitores a escalas distintas no coincide con la
        // nuestra hasta que la ventana ya esta creada ahi.
        float scale = PInvoke.GetDpiForWindow(near) / 96f;
        if (scale <= 0f) scale = 1f;

        return (x, y, w, h, scale);
    }

    /// <summary>
    /// El chip: mismo material que el dock y que la isla, para que las tres utilidades
    /// se vean de la misma familia.
    /// </summary>
    private void Build(Vector2 size)
    {
        ContainerVisual chip = _compositor.CreateContainerVisual();
        chip.RelativeSizeAdjustment = Vector2.One;

        CompositionRoundedRectangleGeometry round = _compositor.CreateRoundedRectangleGeometry();
        round.Size = size;
        round.CornerRadius = new Vector2(14f * _scale);
        chip.Clip = _compositor.CreateGeometricClip(round);

        SpriteVisual material = _compositor.CreateSpriteVisual();
        material.RelativeSizeAdjustment = Vector2.One;
        material.Brush = Visuals.CreateAcrylicBrush();
        chip.Children.InsertAtBottom(material);

        SpriteVisual tint = _compositor.CreateSpriteVisual();
        tint.RelativeSizeAdjustment = Vector2.One;
        tint.Brush = _compositor.CreateColorBrush(Color.FromArgb(52, 255, 255, 255));
        chip.Children.InsertAtTop(tint);

        _root.Children.InsertAtBottom(chip);
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            // Se puede clicar sin que el Explorador pierda el foco.
            case WM_MOUSEACTIVATE:
                return new LRESULT(MA_NOACTIVATE);
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
