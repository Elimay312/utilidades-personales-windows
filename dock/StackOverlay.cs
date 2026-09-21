using System.Numerics;
using System.Runtime.InteropServices;
using Windows.UI;
using Windows.UI.Composition;
using Windows.UI.Composition.Desktop;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.WinRT.Composition;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.UI.WindowsAndMessaging;
using WinRT;

namespace Dock;

/// <summary>
/// La rejilla que se despliega al clicar una carpeta del dock: el "stack" de macOS.
///
/// Hace falta una ventana propia por lo mismo que el genio: el dock mide menos de 200
/// px de alto y una rejilla de veinte elementos no cabe. A diferencia del genio, esta
/// <b>sí</b> recibe clics —es con lo que se abren los elementos—, así que no lleva
/// <c>WS_EX_TRANSPARENT</c>. Lo que sí conserva es <c>WS_EX_NOACTIVATE</c>: clicar aquí
/// tampoco puede robarle el foco a lo que estuvieras usando.
/// </summary>
internal sealed unsafe class StackOverlay : IDisposable
{
    private const string ClassName = "DockStackOverlayClass";

    private const uint WM_DESTROY = 0x0002;
    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const uint WM_MOUSEMOVE = 0x0200;
    private const uint WM_LBUTTONUP = 0x0202;
    private const uint WM_MOUSELEAVE = 0x02A3;
    private const uint WM_TIMER = 0x0113;
    private const nuint CloseTimerId = 1;

    /// Ya se leyó la carpeta a la que se entró y se puede redibujar. WM_APP + 1.
    private const uint WM_APP_RELOAD = 0x8001;
    private const int MA_NOACTIVATE = 3;

    /// IDC_ARROW = MAKEINTRESOURCE(32512)
    private const int IdcArrow = 32512;

    /// <summary>Tamaño de una celda y del icono dentro de ella, en unidades lógicas.</summary>
    private const float LogicalCell = 92f;
    private const float LogicalIcon = 48f;
    private const float LogicalPad = 12f;

    private static readonly WNDPROC WndProcThunk = WndProc;
    private static readonly Dictionary<nint, StackOverlay> Instances = [];
    private static ushort _classAtom;

    private readonly Compositor _compositor;
    private readonly DesktopWindowTarget _target;
    private readonly ContainerVisual _root;
    private readonly DockVisuals _owner;
    private List<StackItem> _items;

    private SpriteVisual? _hot;
    private int _hotIndex = -1;
    private float _cell;
    private int _columns;

    /// <summary>Por dónde se ha ido bajando, para poder volver.</summary>
    private readonly List<string> _history = [];

    /// <summary>Lo leído en segundo plano, esperando a dibujarse.</summary>
    private List<StackItem>? _pending;

    private float _scale;
    private float _anchorX;
    private int _dockTop;
    private RECT _monitor;
    private HWND _hwnd;
    private bool _trackingMouse;
    private bool _disposed;

    private StackOverlay(DockVisuals owner, List<StackItem> items, float scale, int x, int y, int w, int h)
    {
        _owner = owner;
        _compositor = owner.Compositor;
        _items = items;
        _scale = scale;
        _cell = LogicalCell * scale;

        EnsureClassRegistered();

        fixed (char* className = ClassName)
        fixed (char* title = "Stack")
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

        if (_hwnd.IsNull) throw new InvalidOperationException("no se pudo crear la ventana del desplegable");
        Instances[(nint)_hwnd.Value] = this;

        ICompositorDesktopInterop interop = _compositor.As<ICompositorDesktopInterop>();
        interop.CreateDesktopWindowTarget(_hwnd, true, out _target);

        _root = _compositor.CreateContainerVisual();
        _root.RelativeSizeAdjustment = Vector2.One;
        _target.Root = _root;
    }

    private static HINSTANCE ModuleHandle => (HINSTANCE)(nint)PInvoke.GetModuleHandle((PCWSTR)null);

    /// <summary>Qué carpeta está desplegada ahora mismo, o null.</summary>
    public string? Folder { get; private set; }

    /// <summary>Se avisa al cerrarse, para que el dock pueda volver a esconderse.</summary>
    public Action? Closed { get; set; }

    /// <summary>
    /// Despliega la carpeta encima del icono. Devuelve null si no había nada dentro.
    /// </summary>
    public static StackOverlay? Open(
        DockVisuals owner, List<StackItem> items, string folder, float scale, float anchorX, int dockTop, RECT monitor)
    {
        if (items.Count == 0) return null;

        (int columns, int w, int h, int x, int y) = Layout(items.Count, scale, anchorX, dockTop, monitor);

        StackOverlay overlay;
        try
        {
            overlay = new StackOverlay(owner, items, scale, x, y, w, h)
            {
                Folder = folder,
                _columns = columns,
                _anchorX = anchorX,
                _dockTop = dockTop,
                _monitor = monitor,
            };
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[stack] no se pudo abrir: {ex.Message}");
            return null;
        }

        overlay._history.Add(folder);
        overlay.Build(scale, new Vector2(w, h));
        PInvoke.ShowWindow(overlay._hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
        return overlay;
    }

    /// <summary>
    /// Cuántas columnas y qué tamaño y sitio le toca a una rejilla de ese número de
    /// elementos. Se calcula igual al abrir que al entrar en una subcarpeta.
    /// </summary>
    private static (int Columns, int W, int H, int X, int Y) Layout(
        int count, float scale, float anchorX, int dockTop, RECT monitor)
    {
        float cell = LogicalCell * scale;
        float pad = LogicalPad * scale;

        int columns = Math.Min(5, count);
        int rows = (count + columns - 1) / columns;

        int w = (int)MathF.Ceiling(columns * cell + pad * 2f);
        int h = (int)MathF.Ceiling(rows * cell + pad * 2f);

        // Centrada sobre el icono, justo encima del dock, y sin salirse del monitor.
        int x = (int)Math.Clamp(anchorX - w * 0.5f, monitor.left + 8, monitor.right - w - 8);
        int y = Math.Max(monitor.top + 8, dockTop - h - 8);

        return (columns, w, h, x, y);
    }

    /// <summary>
    /// Entra en una subcarpeta, o vuelve a la de arriba. La lectura va en segundo plano
    /// como la primera: son otros veinte iconos del shell.
    /// </summary>
    private void Navigate(string folder, bool back)
    {
        HWND hwnd = _hwnd;
        string? parent = back ? Parent(folder) : _history[^1];

        if (back) _history.RemoveAt(_history.Count - 1);
        else _history.Add(folder);

        Task.Run(() =>
        {
            List<StackItem> items = StackItems.Read(folder, _history.Count > 1 ? parent : null);
            _pending = items;
            PInvoke.PostMessage(hwnd, WM_APP_RELOAD, default, default);
        });
    }

    private static string? Parent(string folder)
    {
        try { return Path.GetDirectoryName(folder); }
        catch { return null; }
    }

    /// <summary>Redibuja con el contenido de la carpeta en la que se acaba de entrar.</summary>
    private void Reload()
    {
        if (_pending is not List<StackItem> items || items.Count == 0) return;
        _pending = null;

        _items = items;
        (int columns, int w, int h, int x, int y) = Layout(items.Count, _scale, _anchorX, _dockTop, _monitor);
        _columns = columns;
        Folder = _history[^1];

        PInvoke.SetWindowPos(_hwnd, default, x, y, w, h,
            SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER);

        _root.Children.RemoveAll();
        _hot = null;
        _hotIndex = -1;
        Build(_scale, new Vector2(w, h));
    }

    private void Build(float scale, Vector2 size)
    {
        float pad = LogicalPad * scale;
        float icon = LogicalIcon * scale;

        // Mismo material que la barra del dock: es una pieza más del dock, no una
        // ventana aparte con su propio aspecto.
        ContainerVisual chip = _compositor.CreateContainerVisual();
        chip.RelativeSizeAdjustment = Vector2.One;

        CompositionRoundedRectangleGeometry round = _compositor.CreateRoundedRectangleGeometry();
        round.Size = size;
        round.CornerRadius = new Vector2(14f * scale);
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

        _hot = _compositor.CreateSpriteVisual();
        _hot.Size = new Vector2(_cell - 4f, _cell - 4f);
        _hot.Brush = _compositor.CreateColorBrush(Color.FromArgb(46, 255, 255, 255));
        _hot.Opacity = 0f;

        CompositionRoundedRectangleGeometry hotRound = _compositor.CreateRoundedRectangleGeometry();
        hotRound.Size = _hot.Size;
        hotRound.CornerRadius = new Vector2(8f * scale);
        _hot.Clip = _compositor.CreateGeometricClip(hotRound);
        _root.Children.InsertAtTop(_hot);

        for (int i = 0; i < _items.Count; i++)
        {
            (float left, float top) = CellAt(i, pad);

            if (_items[i].Icon is IconBitmap bitmap)
            {
                SpriteVisual visual = _compositor.CreateSpriteVisual();
                visual.Size = new Vector2(icon, icon);
                visual.Offset = new Vector3(left + (_cell - icon) * 0.5f, top + pad * 0.5f, 0f);
                visual.Brush = _owner.CreateBitmapBrush(bitmap);
                _root.Children.InsertAtTop(visual);
            }

            // El nombre, recortado al ancho de la celda por la propia etiqueta.
            SpriteVisual label = _compositor.CreateSpriteVisual();
            Vector2 textSize = Labels.Measure(Shorten(_items[i].Name), scale);
            label.Size = textSize;
            label.Offset = new Vector3(
                left + (_cell - textSize.X) * 0.5f,
                top + pad * 0.5f + icon + 4f * scale,
                0f);
            label.Brush = _owner.CreateLabelBrush(Shorten(_items[i].Name), textSize);
            _root.Children.InsertAtTop(label);
        }
    }

    private (float Left, float Top) CellAt(int index, float pad)
        => (pad + index % _columns * _cell, pad + index / _columns * _cell);

    /// <summary>Nombres largos recortados: la celda es la que manda.</summary>
    private static string Shorten(string name)
        => name.Length <= 14 ? name : string.Concat(name.AsSpan(0, 13), "…");

    private int HitTest(int x, int y)
    {
        float pad = LogicalPad * (_cell / LogicalCell);
        int column = (int)((x - pad) / _cell);
        int row = (int)((y - pad) / _cell);

        if (column < 0 || column >= _columns || row < 0) return -1;

        int index = row * _columns + column;
        return index < _items.Count ? index : -1;
    }

    private void SetHot(int index)
    {
        if (_hot is null || index == _hotIndex) return;

        _hotIndex = index;
        _hot.Opacity = index < 0 ? 0f : 1f;

        if (index >= 0)
        {
            (float left, float top) = CellAt(index, LogicalPad * (_cell / LogicalCell));
            _hot.Offset = new Vector3(left + 2f, top + 2f, 0f);
        }
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        Instances.TryGetValue((nint)hwnd.Value, out StackOverlay? self);

        switch (msg)
        {
            // Como el dock: se puede clicar sin que lo que estabas usando pierda el foco.
            case WM_MOUSEACTIVATE:
                return new LRESULT(MA_NOACTIVATE);

            case WM_MOUSEMOVE:
                self?.OnMouseMove(lParam);
                return new LRESULT(0);

            case WM_MOUSELEAVE:
                if (self is not null)
                {
                    self._trackingMouse = false;
                    self.SetHot(-1);

                    // Margen para volver: el camino desde el dock hasta la rejilla pasa
                    // por fuera de ella, y cerrarse en cuanto el ratón sale sería
                    // imposible de usar.
                    PInvoke.SetTimer(hwnd, CloseTimerId, 700, null);
                }
                return new LRESULT(0);

            case WM_TIMER when wParam.Value == CloseTimerId:
                PInvoke.KillTimer(hwnd, CloseTimerId);
                self?.Dispose();
                return new LRESULT(0);

            case WM_LBUTTONUP:
                self?.OnClick(lParam);
                return new LRESULT(0);

            case WM_APP_RELOAD:
                self?.Reload();
                return new LRESULT(0);

            case WM_DESTROY:
                Instances.Remove((nint)hwnd.Value);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    private void OnMouseMove(LPARAM lParam)
    {
        PInvoke.KillTimer(_hwnd, CloseTimerId);

        if (!_trackingMouse)
        {
            TRACKMOUSEEVENT tme = new()
            {
                cbSize = (uint)sizeof(TRACKMOUSEEVENT),
                dwFlags = TRACKMOUSEEVENT_FLAGS.TME_LEAVE,
                hwndTrack = _hwnd,
            };
            PInvoke.TrackMouseEvent(&tme);
            _trackingMouse = true;
        }

        SetHot(HitTest((short)(lParam.Value & 0xFFFF), (short)(lParam.Value >> 16)));
    }

    private void OnClick(LPARAM lParam)
    {
        int index = HitTest((short)(lParam.Value & 0xFFFF), (short)(lParam.Value >> 16));
        if (index < 0) return;

        StackItem item = _items[index];

        // Una carpeta se recorre aquí dentro, que es de lo que va esto: mirar sin tener
        // que abrir el Explorador entero.
        if (item.IsFolder)
        {
            bool volviendo = item.Name == "Atrás";
            Console.WriteLine($"[stack] {(volviendo ? "vuelve a" : "entra en")} {item.Target}");
            Navigate(item.Target, volviendo);
            return;
        }

        string target = item.Target;
        Console.WriteLine($"[stack] abre {target}");

        // Igual que al lanzar desde el dock: fuera del hilo que atiende el ratón.
        Task.Run(() =>
        {
            try
            {
                System.Diagnostics.Process.Start(
                    new System.Diagnostics.ProcessStartInfo(target) { UseShellExecute = true });
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[stack] no se pudo abrir {target}: {ex.Message}");
            }
        });

        Dispose();
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

        if (!_hwnd.IsNull)
        {
            PInvoke.DestroyWindow(_hwnd);
            _hwnd = default;
        }

        _target.Root = null;
        _root.Dispose();
        _target.Dispose();

        Closed?.Invoke();
    }
}
