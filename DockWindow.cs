using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.Graphics.Dwm;
using Windows.Win32.Graphics.Gdi;
using Windows.Win32.UI.Controls;
using Windows.Win32.UI.HiDpi;
using Windows.Win32.UI.Input.KeyboardAndMouse;
using Windows.Win32.UI.WindowsAndMessaging;

namespace Dock;

/// <summary>
/// Una ventana de dock, anclada a un monitor concreto.
/// Se parametriza por monitor desde M0 aunque solo se instancie una: en M4 se crea
/// una por pantalla y así no hay que rehacer nada.
/// </summary>
internal sealed unsafe class DockWindow : IDisposable
{
    private const string ClassName = "DockWindowClass";

    // Medidas en unidades lógicas (a 96 DPI). Se escalan por el DPI del monitor.
    private const int LogicalHeight = 72;
    private const int LogicalWidth = 420;
    private const int LogicalBottomMargin = 8;

    // Mensajes que manejamos. Se declaran aquí para no arrastrar cientos de
    // constantes desde la metadata del SDK.
    private const uint WM_DESTROY = 0x0002;
    private const uint WM_PAINT = 0x000F;
    private const uint WM_ERASEBKGND = 0x0014;
    private const uint WM_MOUSEACTIVATE = 0x0021;
    private const uint WM_NCCALCSIZE = 0x0083;
    private const uint WM_MOUSEMOVE = 0x0200;
    private const uint WM_RBUTTONUP = 0x0205;
    private const uint WM_MOUSELEAVE = 0x02A3;
    private const uint WM_DPICHANGED = 0x02E0;

    // IDC_ARROW = MAKEINTRESOURCE(32512)
    private const int IdcArrow = 32512;

    // Respuesta a WM_MOUSEACTIVATE: no activar, pero tampoco descartar el clic.
    private const int MA_NOACTIVATE = 3;

    // DWMWA_BORDER_COLOR: quita el borde manteniendo el redondeo.
    private const uint DwmwaColorNone = 0xFFFFFFFE;

    /// El delegate se guarda en un campo estático para que el GC no lo recoja
    /// mientras Windows conserva el puntero dentro de la clase de ventana.
    private static readonly WNDPROC WndProcThunk = WndProc;

    /// Un dock por HWND. En M0 solo hay una entrada; en M4, una por monitor.
    private static readonly Dictionary<nint, DockWindow> Windows = [];

    private static ushort _classAtom;

    private readonly HMONITOR _monitor;

    private HWND _hwnd;
    private uint _dpi = 96;
    private bool _trackingMouse;

    public DockWindow(HMONITOR monitor)
    {
        _monitor = monitor;
        EnsureClassRegistered();
        Create();
    }

    /// <summary>Monitor principal. En M4 esto lo sustituye EnumDisplayMonitors.</summary>
    public static HMONITOR PrimaryMonitor =>
        PInvoke.MonitorFromPoint(default, MONITOR_FROM_FLAGS.MONITOR_DEFAULTTOPRIMARY);

    private static HINSTANCE ModuleHandle
    {
        get
        {
            HMODULE module = PInvoke.GetModuleHandle((PCWSTR)null);
            return (HINSTANCE)(nint)module;
        }
    }

    private static void EnsureClassRegistered()
    {
        if (_classAtom != 0) return;

        fixed (char* className = ClassName)
        {
            WNDCLASSEXW wc = new()
            {
                // Marshal.SizeOf y no sizeof: WNDCLASSEXW lleva un delegate, asi que el
                // compilador la trata como tipo administrado y su sizeof no es el
                // tamano nativo que espera RegisterClassEx.
                cbSize = (uint)Marshal.SizeOf<WNDCLASSEXW>(),
                lpfnWndProc = WndProcThunk,
                hInstance = ModuleHandle,
                lpszClassName = new PCWSTR(className),
                hCursor = PInvoke.LoadCursor(default, new PCWSTR((char*)IdcArrow)),
                hbrBackground = default,
            };

            _classAtom = PInvoke.RegisterClassEx(in wc);
            if (_classAtom == 0)
                throw new InvalidOperationException($"RegisterClassEx falló: {Marshal.GetLastWin32Error()}");
        }
    }

    private void Create()
    {
        (int x, int y, int w, int h) = ComputeBounds();

        // WS_CAPTION es obligatorio aunque no queramos barra de título: verificado en
        // M0, DWM ignora el backdrop en una WS_POPUP pura y la ventana sale invisible.
        // WM_NCCALCSIZE colapsa después el área no cliente, así que no se dibuja
        // barra ni se puede arrastrar.
        const WINDOW_STYLE Style = WINDOW_STYLE.WS_POPUP | WINDOW_STYLE.WS_CAPTION;

        fixed (char* className = ClassName)
        fixed (char* title = "Dock")
        {
            _hwnd = PInvoke.CreateWindowEx(
                // NOACTIVATE: no roba el foco. TOOLWINDOW: fuera de Alt+Tab y de la
                // barra de tareas. TOPMOST: siempre encima.
                WINDOW_EX_STYLE.WS_EX_NOACTIVATE
                    | WINDOW_EX_STYLE.WS_EX_TOOLWINDOW
                    | WINDOW_EX_STYLE.WS_EX_TOPMOST,
                new PCWSTR(className),
                new PCWSTR(title),
                Style,
                x, y, w, h,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull)
            throw new InvalidOperationException($"CreateWindowEx falló: {Marshal.GetLastWin32Error()}");

        Windows[(nint)_hwnd.Value] = this;
        ApplyDwmAttributes();
    }

    /// <summary>Rectángulo del dock en píxeles físicos, centrado abajo en su monitor.</summary>
    private (int X, int Y, int W, int H) ComputeBounds()
    {
        MONITORINFO info = new() { cbSize = (uint)sizeof(MONITORINFO) };
        PInvoke.GetMonitorInfo(_monitor, &info);

        PInvoke.GetDpiForMonitor(_monitor, MONITOR_DPI_TYPE.MDT_EFFECTIVE_DPI, out uint dpiX, out _);
        _dpi = dpiX;

        int Scale(int logical) => (int)Math.Round(logical * dpiX / 96.0);

        int w = Scale(LogicalWidth);
        int h = Scale(LogicalHeight);
        RECT work = info.rcWork;
        int x = work.left + ((work.right - work.left) - w) / 2;
        int y = work.bottom - h - Scale(LogicalBottomMargin);

        return (x, y, w, h);
    }

    private void ApplyDwmAttributes()
    {
        // Esquinas redondeadas. La doc lo llama un hint, no una garantía.
        DWM_WINDOW_CORNER_PREFERENCE corner = DWM_WINDOW_CORNER_PREFERENCE.DWMWCP_ROUND;
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_WINDOW_CORNER_PREFERENCE,
            &corner, (uint)sizeof(DWM_WINDOW_CORNER_PREFERENCE));

        // Borde invisible, manteniendo el redondeo.
        uint border = DwmwaColorNone;
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_BORDER_COLOR,
            &border, sizeof(uint));

        // Extender el marco a toda el área cliente: sin esto DWM no tiene dónde
        // pintar el material.
        MARGINS margins = new() { cxLeftWidth = -1, cxRightWidth = -1, cyTopHeight = -1, cyBottomHeight = -1 };
        PInvoke.DwmExtendFrameIntoClientArea(_hwnd, &margins);

        DWM_SYSTEMBACKDROP_TYPE backdrop = DWM_SYSTEMBACKDROP_TYPE.DWMSBT_TRANSIENTWINDOW;
        PInvoke.DwmSetWindowAttribute(_hwnd, DWMWINDOWATTRIBUTE.DWMWA_SYSTEMBACKDROP_TYPE,
            &backdrop, (uint)sizeof(DWM_SYSTEMBACKDROP_TYPE));
    }

    public void Show()
    {
        // SW_SHOWNOACTIVATE: mostrar sin activar, coherente con WS_EX_NOACTIVATE.
        PInvoke.ShowWindow(_hwnd, SHOW_WINDOW_CMD.SW_SHOWNOACTIVATE);
        Console.WriteLine($"[dock] HWND=0x{(nint)_hwnd.Value:X} DPI={_dpi} ({_dpi * 100 / 96}%)");
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        Windows.TryGetValue((nint)hwnd.Value, out DockWindow? self);

        switch (msg)
        {
            // WS_EX_NOACTIVATE por sí solo NO basta: verificado en M0, al hacer clic
            // llegaban WM_ACTIVATE(WA_CLICKACTIVE) y WM_SETFOCUS. MA_NOACTIVATE
            // rechaza la activación sin descartar el clic, que es lo que M1 necesita
            // para lanzar apps.
            case WM_MOUSEACTIVATE:
                return new LRESULT(MA_NOACTIVATE);

            // Colapsa el área no cliente. No depende de la instancia a propósito:
            // este mensaje llega durante CreateWindowEx, cuando la ventana todavía no
            // está en el diccionario.
            case WM_NCCALCSIZE when wParam.Value != 0:
                return new LRESULT(0);

            case WM_ERASEBKGND:
                // No borrar: el fondo lo pinta OnPaint de una sola vez.
                return new LRESULT(1);

            case WM_PAINT:
                self?.OnPaint();
                return new LRESULT(0);

            case WM_MOUSEMOVE:
                self?.OnMouseMove(lParam);
                return new LRESULT(0);

            case WM_MOUSELEAVE:
                if (self is not null) self._trackingMouse = false;
                return new LRESULT(0);

            case WM_RBUTTONUP:
                // Única vía de salida en M0: la ventana no sale en Alt+Tab.
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);

            case WM_DPICHANGED:
                self?.OnDpiChanged(wParam, lParam);
                return new LRESULT(0);

            case WM_DESTROY:
                Windows.Remove((nint)hwnd.Value);
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    /// <summary>
    /// Pintado provisional de M0 con GDI. M1 lo sustituye entero por un árbol de
    /// visuals de Composition, así que aquí no se invierte más de lo justo.
    /// </summary>
    private void OnPaint()
    {
        PAINTSTRUCT ps;
        HDC hdc = PInvoke.BeginPaint(_hwnd, &ps);

        // Con el marco extendido, TODA el área cliente tiene que quedar definida o
        // DWM embarra el buffer anterior. El negro de GDI es lo que DWM interpreta
        // como transparente: es ahí donde pinta el material.
        RECT client;
        PInvoke.GetClientRect(_hwnd, &client);
        HBRUSH black = PInvoke.CreateSolidBrush(new COLORREF(0x00000000));
        PInvoke.FillRect(hdc, &client, black);
        PInvoke.DeleteObject((HGDIOBJ)(nint)black);

        // Marcador opaco, para localizar la ventana aunque el material no se pinte.
        int size = (int)Math.Round(24 * _dpi / 96.0);
        RECT marker = new() { left = 0, top = 0, right = size, bottom = size };
        HBRUSH brush = PInvoke.CreateSolidBrush(new COLORREF(0x004040FF));
        PInvoke.FillRect(hdc, &marker, brush);
        PInvoke.DeleteObject((HGDIOBJ)(nint)brush);

        PInvoke.EndPaint(_hwnd, &ps);
    }

    private void OnMouseMove(LPARAM lParam)
    {
        if (!_trackingMouse)
        {
            // TrackMouseEvent se desarma solo al dispararse: hay que re-armarlo en
            // cada entrada.
            TRACKMOUSEEVENT tme = new()
            {
                cbSize = (uint)sizeof(TRACKMOUSEEVENT),
                dwFlags = TRACKMOUSEEVENT_FLAGS.TME_LEAVE,
                hwndTrack = _hwnd,
            };
            PInvoke.TrackMouseEvent(&tme);
            _trackingMouse = true;
        }

        short x = (short)(lParam.Value & 0xFFFF);
        short y = (short)((lParam.Value >> 16) & 0xFFFF);
        Console.WriteLine($"[raton] x={x} y={y}");
    }

    private void OnDpiChanged(WPARAM wParam, LPARAM lParam)
    {
        _dpi = (uint)(wParam.Value & 0xFFFF);
        RECT* suggested = (RECT*)lParam.Value;
        PInvoke.SetWindowPos(_hwnd, default,
            suggested->left, suggested->top,
            suggested->right - suggested->left, suggested->bottom - suggested->top,
            SET_WINDOW_POS_FLAGS.SWP_NOACTIVATE | SET_WINDOW_POS_FLAGS.SWP_NOZORDER);

        Console.WriteLine($"[dpi] WM_DPICHANGED -> {_dpi} ({_dpi * 100 / 96}%)");
    }

    public static void RunMessageLoop()
    {
        MSG msg;
        while (PInvoke.GetMessage(&msg, default, 0, 0).Value > 0)
        {
            PInvoke.TranslateMessage(&msg);
            PInvoke.DispatchMessage(&msg);
        }
    }

    public void Dispose()
    {
        if (_hwnd.IsNull) return;
        PInvoke.DestroyWindow(_hwnd);
        _hwnd = default;
    }
}
