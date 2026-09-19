using System.Runtime.InteropServices;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.UI.WindowsAndMessaging;

namespace QuickLook;

/// <summary>
/// La ventana que nunca se ve.
///
/// <para>
/// Existe por dos razones y ninguna es dibujar. La primera: <c>WH_KEYBOARD_LL</c> exige
/// que el hilo que lo instala tenga un bucle de mensajes, porque el sistema entrega el
/// callback mandandole un mensaje a ese hilo. La segunda: el callback del hook no puede
/// hacer trabajo —ver <c>Hook.cs</c>— asi que le manda un <c>WM_APP</c> aqui y es
/// este <c>WndProc</c> quien abre el panel, ya fuera del camino critico.
/// </para>
///
/// <para>
/// No se registra como <c>HWND_MESSAGE</c>. Una ventana de solo mensajes valdria hoy,
/// pero no recibe mensajes de difusion y el panel acabara queriendo enterarse de cambios
/// de pantalla y de DPI. Una ventana normal que nunca se muestra no cuesta nada mas.
/// </para>
/// </summary>
internal sealed unsafe class HostWindow : IDisposable
{
    private const string ClassName = "QuickLookHostClass";

    private const uint WM_DESTROY = 0x0002;
    private const uint WM_TIMER = 0x0113;

    /// <summary>
    /// Cada cuanto se mira quien esta delante mientras hay panel abierto. 200 ms es por
    /// debajo de lo que se nota y solo corre mientras el panel existe: con el panel
    /// cerrado no hay temporizador, no hay hilos y no hay sondeo. Ver SEGURIDAD.md §5.
    /// </summary>
    private const uint WatchTimer = 1;
    private const uint WatchMs = 200;

    /// <summary>El hook pide abrir o cerrar el panel. Lo manda <c>Hook.cs</c>.</summary>
    internal const uint WM_APP_QUICKLOOK = 0x8001;

    // El delegado se guarda en un campo estatico a proposito: si se pasara directamente
    // a WNDCLASSEXW, el GC podria recogerlo mientras Windows todavia tiene el puntero, y
    // el fallo aparece mucho despues y en otro sitio.
    private static readonly WNDPROC WndProcThunk = WndProc;

    private static ushort _classAtom;

    // Solo hay una ventana-host, asi que el WndProc estatico la encuentra por aqui en
    // vez de montar un diccionario para una sola entrada.
    private static HostWindow? _instance;

    private HWND _hwnd;
    private Panel? _panel;
    private bool _disposed;

    public HostWindow()
    {
        EnsureClassRegistered();

        fixed (char* className = ClassName)
        fixed (char* title = "QuickLook")
        {
            _hwnd = PInvoke.CreateWindowEx(
                WINDOW_EX_STYLE.WS_EX_TOOLWINDOW,
                new PCWSTR(className),
                new PCWSTR(title),
                WINDOW_STYLE.WS_POPUP,
                0, 0, 0, 0,
                default, default, ModuleHandle, null);
        }

        if (_hwnd.IsNull) throw new InvalidOperationException("no se pudo crear la ventana-host");
        _instance = this;
        Panel.Host = _hwnd;
    }

    /// <summary>Donde el hook deja su aviso. Nunca se muestra.</summary>
    public HWND Handle => _hwnd;

    private static HINSTANCE ModuleHandle => (HINSTANCE)(nint)PInvoke.GetModuleHandle((PCWSTR)null);

    public void RunMessageLoop()
    {
        MSG msg;
        while (PInvoke.GetMessage(&msg, default, 0, 0).Value > 0)
        {
            PInvoke.TranslateMessage(&msg);
            PInvoke.DispatchMessage(&msg);
        }
    }

    private static LRESULT WndProc(HWND hwnd, uint msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            // El aviso del hook. Aqui ya estamos fuera del callback, asi que se puede
            // tardar lo que haga falta sin que Windows desinstale el hook.
            case WM_APP_QUICKLOOK:
                _instance?.Toggle();
                return new LRESULT(0);

            // Mientras hay panel: si el usuario se ha ido a otra app, el panel sobra.
            // Sin esto la unica salida era volver al Explorador y pulsar espacio otra
            // vez, que es lo que hacia que la ventana pareciese imposible de cerrar.
            case WM_TIMER when wParam.Value == WatchTimer:
                _instance?.CloseIfAway();
                return new LRESULT(0);

            case WM_DESTROY:
                PInvoke.PostQuitMessage(0);
                return new LRESULT(0);
        }

        return PInvoke.DefWindowProc(hwnd, msg, wParam, lParam);
    }

    /// <summary>
    /// Espacio: si no hay panel lo abre, y si lo hay lo cierra. Es el gesto entero.
    /// </summary>
    private void Toggle()
    {
        if (_panel is not null)
        {
            Close();
            return;
        }

        // La ventana en primer plano es la del Explorador: el hook ya lo comprobo. De
        // ella salen las dos cosas que hacen falta: que archivo hay seleccionado, y en
        // que monitor y a que escala se dibuja el panel.
        HWND front = PInvoke.GetForegroundWindow();

        string? path = Selection.Path(front);
        if (path is null)
        {
            // Sin nada seleccionado no hay nada que ensenar. El espacio ya se lo comio el
            // hook, pero abrir un panel vacio seria peor.
            Console.WriteLine("[seleccion] nada seleccionado");
            return;
        }

        Console.WriteLine($"[seleccion] {path}");
        _panel = Panel.Open(front, Preview.For(path));

        if (_panel is not null) PInvoke.SetTimer(_hwnd, WatchTimer, WatchMs, null);
    }

    /// <summary>Cierra el panel y para el temporizador. Es el unico camino de cierre.</summary>
    private void Close()
    {
        if (_panel is null) return;

        PInvoke.KillTimer(_hwnd, WatchTimer);
        _panel.Dispose();
        _panel = null;
    }

    /// <summary>
    /// Si delante ya no hay ni el Explorador ni nuestro propio panel, el usuario se ha ido
    /// a otra cosa y el panel se cierra solo.
    ///
    /// El panel cuenta como "delante" aunque nunca tome el foco: se comprueba por HWND, no
    /// por foco, precisamente porque no lo roba.
    /// </summary>
    private void CloseIfAway()
    {
        if (_panel is null) return;

        HWND front = PInvoke.GetForegroundWindow();
        if (front == _panel.Handle || Foreground.IsExplorer(front)) return;

        Close();
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
            };

            _classAtom = PInvoke.RegisterClassEx(in wc);
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;

        Close();

        if (!_hwnd.IsNull)
        {
            PInvoke.DestroyWindow(_hwnd);
            _hwnd = default;
        }

        _instance = null;
    }
}
