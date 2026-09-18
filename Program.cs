using System.Text;
using Dock;

// La consola de Windows usa la codificación ANSI del sistema por defecto y
// destroza los acentos.
Console.OutputEncoding = Encoding.UTF8;

Console.WriteLine("Dock M0 — clic derecho sobre el dock para salir");
Console.WriteLine();

// M0: una sola ventana en el monitor principal. En M4 pasa a una por pantalla.
using var dock = new DockWindow(DockWindow.PrimaryMonitor);
dock.Show();
DockWindow.RunMessageLoop();

Console.WriteLine("[dock] salida limpia");
