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

    /// <summary>El hook pide abrir o cerrar el panel. Lo manda <c>Hook.cs</c>.</summary>
    internal const uint WM_APP_QUICKLOOK = 0x8001;

    // El delegado se guarda en un campo estatico a proposito: si se pasara directamente
    // a WNDCLASSEXW, el GC podria recogerlo mientras Windows todavia tiene el puntero, y
    // el fallo aparece mucho despues y en otro sitio.
    private static readonly WNDPROC WndProcThunk = WndProc;

    private static ushort _classAtom;

    private HWND _hwnd;
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
            case WM_DESTROY:
                PInvoke.PostQuitMessage(0);
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
    }
}
