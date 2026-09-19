using System.Diagnostics;
using System.Text;
using Windows.Win32;

namespace Lanzador;

internal static class Program
{
    // Local\ y no Global\: el ambito es la sesion del usuario, que es donde hay un
    // teclado y un atajo. Global\ necesitaria permisos que esta app no tiene ni quiere.
    private const string MutexName = @"Local\Lanzador.instancia";

    [STAThread]
    private static int Main(string[] args)
    {
        // Compilado como WinExe no hay consola propia. Si nos han lanzado desde una
        // terminal nos enganchamos a la suya; si no, no hay donde escribir y da igual.
        // El guardia evita romper una tuberia cuando la salida esta redirigida.
        if (!Console.IsOutputRedirected) PInvoke.AttachConsole(0xFFFFFFFF);
        Console.OutputEncoding = Encoding.UTF8;

        // Los modos de consola no abren ventana ni registran el atajo, asi que no
        // compiten con la instancia que ya este corriendo y no piden el mutex.
        switch (args.Length > 0 ? args[0] : string.Empty)
        {
            case "--check":   return Check();
            case "--indice":  return VolcarIndice(args.Length > 1 && args[1] == "--todo");
            case "--buscar":  return Buscar(args.Length > 1 ? args[1] : string.Empty);
            case "--olvidar": return Olvidar();
            case "--ayuda" or "-h" or "/?": return Ayuda();
        }

        // Un solo lanzador: dos procesos serian dos ventanas peleandose por el mismo
        // atajo, y RegisterHotKey se lo daria solo al primero.
        using Mutex unica = new(true, MutexName, out bool primera);
        if (!primera)
        {
            Console.WriteLine("[lanzador] ya hay uno corriendo.");
            return 1;
        }

        Console.Error.WriteLine("[lanzador] todavia no hay ventana: H0 es solo el andamio.");
        return Ayuda();
    }

    private static int Ayuda()
    {
        Console.WriteLine("""
            lanzador [modo]

              --check          comprueba el algoritmo, el decaimiento y la calculadora
              --indice         vuelca las aplicaciones encontradas y cuanto costo
              --buscar TEXTO   los mejores resultados, con su puntuacion desglosada
              --olvidar        borra uso.json entero
              sin modo         arranca y se queda esperando el atajo
            """);
        return 0;
    }

    // Los cuatro modos de consola se van rellenando en su hito. Devuelven 2 —y lo dicen—
    // en vez de fingir que pasaron: una comprobacion que aprueba sin mirar es peor que
    // no tenerla.
    private static int NoTodavia(string que, string hito)
    {
        Console.Error.WriteLine($"[lanzador] {que} llega en {hito}.");
        return 2;
    }

    /// <summary>
    /// Los casos del algoritmo, con candidatos inventados. <b>Inventados a proposito</b>:
    /// si dependieran del menu Inicio de esta maquina, dejarian de comprobar nada en
    /// cuanto se instalase o desinstalase algo.
    /// </summary>
    private static readonly (string Consulta, string Esperado, string[] Contra, string Porque)[] CasosDelRanking =
    {
        ("br", "Brave", ["Brave", "Game Bar", "Barony"],
         "el prefijo gana a caer a mitad de palabra"),

        ("conf", "Configuracion", ["Configuracion del sistema", "Configuracion"],
         "a igualdad, gana el nombre corto"),

        ("configuracion", "Configuración", ["Configuración"],
         "los acentos no cuentan al escribir"),

        ("abc", "abc", ["abc", "a_b_c", "axbxc"],
         "las letras seguidas ganan a las separadas"),

        ("axbxc", "axbxc", ["abc", "axbxc"],
         "pero si escribes las de enmedio, gana el que las tiene"),

        // Ojo con la tentacion de meter aqui "Vs Code Cosa": gana a "Visual Studio Code"
        // (108 a 62) y esta BIEN que gane, porque textualmente es mejor coincidencia —
        // "vs" seguidas y desde el principio. Lo que hace que en la vida real salga
        // primero el que quieres no es el texto, es el ranking por uso de H3.
        ("vsc", "Visual Studio Code", ["Visual Studio Code", "avascular"],
         "empezar palabra gana a caer dentro de una"),

        ("vsc", "VisualStudioCode", ["VisualStudioCode", "visualstudiocode"],
         "y la mayuscula de un camelCase tambien empieza palabra"),

        ("term", "Terminal", ["Terminal", "Character Map", "Computer Management"],
         "el prefijo gana a dos trozos sueltos"),
    };

    private static int Check()
    {
        int fallos = 0;

        Console.WriteLine("Coincidencia — orden de los resultados");
        foreach ((string consulta, string esperado, string[] contra, string porque) in CasosDelRanking)
        {
            List<Entrada> candidatos = contra.Select(c => new Entrada(c, c)).ToList();
            List<Resultado> orden = Coincidencia.Buscar(candidatos, consulta, contra.Length);
            string primero = orden.Count > 0 ? orden[0].Entrada.Nombre : "(nada)";

            bool bien = primero == esperado;
            if (!bien) fallos++;
            Console.WriteLine($"  {(bien ? "ok  " : "FALLA")} \"{consulta}\" -> {primero,-26} {porque}");
            if (!bien)
            {
                // Que puntuo cada uno y que se comparo de verdad. Sin esto, un caso que
                // falla solo dice que fallo, y el siguiente paso es adivinar.
                string q = Coincidencia.Normalizar(consulta).ToLowerInvariant().Trim();
                Console.WriteLine($"         esperaba \"{esperado}\", consulta normalizada \"{q}\"");
                foreach (Entrada e in candidatos)
                {
                    int p = Coincidencia.Puntuar(e.Buscable, q);
                    Console.WriteLine($"           \"{e.Buscable}\"  filtro={Coincidencia.Contiene(e.Buscable, q)}  " +
                                      $"puntos={(p == Coincidencia.NoCoincide ? "no coincide" : p.ToString())}");
                }
            }
        }

        Console.WriteLine();
        Console.WriteLine("Coincidencia — casos limite");
        fallos += Exige("consulta vacia no devuelve nada",
                        Coincidencia.Buscar([new Entrada("Brave", "x")], "", 10).Count == 0);
        fallos += Exige("consulta mas larga que el candidato no coincide",
                        Coincidencia.Puntuar("ab", "abcd") == Coincidencia.NoCoincide);
        fallos += Exige("letras en otro orden no coinciden",
                        Coincidencia.Puntuar("Brave", "rb") == Coincidencia.NoCoincide);
        fallos += Exige("una letra que no esta no coincide",
                        Coincidencia.Puntuar("Brave", "z") == Coincidencia.NoCoincide);
        fallos += Exige("el candidato entero coincide consigo mismo",
                        Coincidencia.Puntuar("brave", "brave") != Coincidencia.NoCoincide);

        // El filtro barato solo puede descartar lo que la puntuacion tambien descartaria.
        // Si se desincronizan, el filtro empieza a esconder resultados buenos y no hay
        // forma de notarlo mirando la pantalla.
        Console.WriteLine();
        Console.WriteLine("Coincidencia — el filtro barato concuerda con la puntuacion");
        string[] alfabeto = ["Brave", "Configuración", "Visual Studio Code", "abc", "a_b_c", "Administrador de tareas"];
        int desacuerdos = 0, probados = 0;
        Random dado = new(1);   // semilla fija: un fallo tiene que poder repetirse
        for (int i = 0; i < 4000; i++)
        {
            string candidato = Coincidencia.Normalizar(alfabeto[dado.Next(alfabeto.Length)]);
            int largo = dado.Next(1, 5);
            string consulta = new(Enumerable.Range(0, largo)
                .Select(_ => "abcdeirstuvox"[dado.Next(13)]).ToArray());

            probados++;
            bool pasaFiltro = Coincidencia.Contiene(candidato, consulta);
            bool puntua = Coincidencia.Puntuar(candidato, consulta) != Coincidencia.NoCoincide;
            if (pasaFiltro != puntua)
            {
                desacuerdos++;
                if (desacuerdos <= 3) Console.WriteLine($"  FALLA \"{consulta}\" sobre \"{candidato}\": filtro={pasaFiltro} puntua={puntua}");
            }
        }
        fallos += Exige($"{probados} consultas al azar, ningun desacuerdo", desacuerdos == 0);

        // --- El ranking por uso (H3) ------------------------------------------------
        // Con fechas fijas y un Uso construido a mano: si esto leyera el uso.json de
        // verdad, la comprobacion diria una cosa distinta cada dia.
        Console.WriteLine();
        Console.WriteLine("Uso — el refuerzo decae con el tiempo");
        DateTimeOffset hoy = new(2026, 9, 19, 12, 0, 0, TimeSpan.Zero);
        Uso uso = new();
        uso.Lanzamientos["reciente"] = new Lanzamiento { Veces = 5, Ultimo = hoy };
        uso.Lanzamientos["hace30"] = new Lanzamiento { Veces = 5, Ultimo = hoy.AddDays(-30) };
        uso.Lanzamientos["hace180"] = new Lanzamiento { Veces = 5, Ultimo = hoy.AddDays(-180) };

        int hoyPuntos = uso.Refuerzo("reciente", hoy);
        int mesPuntos = uso.Refuerzo("hace30", hoy);
        int semestre = uso.Refuerzo("hace180", hoy);
        Console.WriteLine($"       hoy {hoyPuntos}   hace 30 dias {mesPuntos}   hace 180 dias {semestre}");
        fallos += Exige("lo de hoy pesa mas que lo de hace un mes", hoyPuntos > mesPuntos);
        fallos += Exige("y lo de hace un mes mas que lo de hace medio ano", mesPuntos > semestre);
        fallos += Exige("a los 30 dias vale la mitad (semivida)", mesPuntos * 2 == hoyPuntos);
        fallos += Exige("lo que no has abierto nunca no suma", uso.Refuerzo("jamas", hoy) == 0);
        fallos += Exige("un reloj que va hacia atras no premia",
                        uso.Refuerzo("reciente", hoy.AddDays(-5)) == hoyPuntos);

        // Sin saturar, algo abierto trescientas veces sepultaria todo lo demas para
        // siempre y el lanzador dejaria de aprender.
        Uso muchas = new();
        muchas.Lanzamientos["a"] = new Lanzamiento { Veces = 10, Ultimo = hoy };
        muchas.Lanzamientos["b"] = new Lanzamiento { Veces = 300, Ultimo = hoy };
        fallos += Exige("las veces saturan: 300 no puntua mas que 10",
                        muchas.Refuerzo("b", hoy) == muchas.Refuerzo("a", hoy));

        Console.WriteLine();
        Console.WriteLine("Uso — lo que elegiste manda");
        List<Entrada> dos = [new Entrada("Brave", "brave.lnk"), new Entrada("Br", "br.lnk")];

        List<Resultado> sinUso = Coincidencia.Buscar(dos, "br", 2);
        fallos += Exige("sin uso, gana el que puntua mejor de texto (Br)",
                        sinUso[0].Entrada.Nombre == "Br");

        Uso fijado = new();
        fijado.Registrar("br", "brave.lnk", hoy);
        List<Resultado> conFijado = Coincidencia.Buscar(dos, "br", 2, fijado, hoy);
        fallos += Exige("con la eleccion fijada, gana Brave aunque puntue peor de texto",
                        conFijado[0].Entrada.Nombre == "Brave");
        // Con "b" tambien gana Brave, pero por otro motivo: el refuerzo normal de haberlo
        // abierto una vez. Lo que se comprueba aqui es que NO lleva el bono de fijado, que
        // es lo unico que no debe salirse de su consulta. Comprobar quien sale primero
        // seria comprobar el peso, no el mecanismo, y cambiaria al afinar los pesos.
        List<Resultado> otraConsulta = Coincidencia.Buscar(dos, "b", 2, fijado, hoy);
        fallos += Exige("el bono de fijado no se escapa a otra consulta",
                        otraConsulta.TrueForAll(r => r.Costumbre < Uso.BonoDeFijado));

        // Lo que se guarda es lo que lanzaste, no lo que escribiste (regla 11).
        fallos += Exige("registrar guarda el destino, no la consulta suelta",
                        fijado.Lanzamientos.ContainsKey("brave.lnk") && fijado.Lanzamientos.Count == 1);

        Console.WriteLine();
        if (fallos == 0) { Console.WriteLine("TODO BIEN"); return 0; }
        Console.WriteLine($"{fallos} comprobacion(es) fallan");
        return 1;
    }

    private static int Exige(string que, bool secumple)
    {
        Console.WriteLine($"  {(secumple ? "ok  " : "FALLA")} {que}");
        return secumple ? 0 : 1;
    }

    /// <summary>
    /// Cuantas aplicaciones hay, de donde salen y cuanto costo. El desglose por fuente se
    /// queda: es lo que contesta "me falta una app" sin tener que adivinar en cual de las
    /// dos deberia estar.
    /// </summary>
    private static int VolcarIndice(bool todo)
    {
        Stopwatch reloj = Stopwatch.StartNew();
        List<Entrada> appsFolder = Indice.AppsFolder();
        long msFolder = reloj.ElapsedMilliseconds;

        reloj.Restart();
        List<Entrada> menus = Indice.MenusInicio();
        long msMenus = reloj.ElapsedMilliseconds;

        HashSet<string> enFolder = new(appsFolder.Select(e => e.Nombre), StringComparer.OrdinalIgnoreCase);
        int soloEnMenus = menus.Count(e => !enFolder.Contains(e.Nombre));

        Console.WriteLine($"shell:AppsFolder   {appsFolder.Count,5}   {msFolder,4} ms");
        Console.WriteLine($"menus Inicio .lnk  {menus.Count,5}   {msMenus,4} ms   ({soloEnMenus} no estan en AppsFolder)");
        Console.WriteLine($"indice             {appsFolder.Count + soloEnMenus,5}   {msFolder + msMenus,4} ms");
        Console.WriteLine();

        List<Entrada> indice = Indice.Construir();
        int cuantas = todo ? indice.Count : 40;
        foreach (Entrada e in indice.OrderBy(e => e.Nombre, StringComparer.OrdinalIgnoreCase).Take(cuantas))
        {
            Console.WriteLine($"  {e.Nombre,-45} {e.Destino}");
        }
        if (cuantas < indice.Count) Console.WriteLine($"  ... y {indice.Count - cuantas} mas (--indice --todo)");

        return 0;
    }

    /// <summary>
    /// Los mejores resultados con el desglose de su puntuacion. Esto es lo que hace que
    /// afinar los pesos no sea adivinar: se ve en que letras cayo la consulta y cuanto
    /// puso cada concepto.
    /// </summary>
    private static int Buscar(string consulta)
    {
        if (consulta.Length == 0)
        {
            Console.Error.WriteLine("[lanzador] --buscar necesita algo que buscar.");
            return 2;
        }

        List<Entrada> indice = Indice.Construir();
        Uso uso = Uso.Cargar();
        DateTimeOffset ahora = DateTimeOffset.UtcNow;

        // Diez pasadas para que el reloj tenga algo que medir: una sola consulta esta por
        // debajo de la resolucion del cronometro y saldria siempre 0 ms.
        Stopwatch reloj = Stopwatch.StartNew();
        List<Resultado> mejores = new();
        for (int i = 0; i < 10; i++) mejores = Coincidencia.Buscar(indice, consulta, 10, uso, ahora);
        double ms = reloj.Elapsed.TotalMilliseconds / 10;

        Console.WriteLine($"\"{consulta}\"  sobre {indice.Count} entradas  {ms:0.00} ms por consulta" +
                          $"  ({uso.Lanzamientos.Count} cosas en uso.json)");
        Console.WriteLine();

        string q = Coincidencia.Normalizar(consulta).ToLowerInvariant().Trim();
        foreach (Resultado r in mejores)
        {
            Desglose? d = Coincidencia.Explicar(r.Entrada.Buscable, q);
            if (d is null) continue;

            Console.WriteLine($"  {r.Puntos,5}  {Marcado(r.Entrada.Buscable, d.Donde)}");
            Console.WriteLine($"         letras {d.Letras}   prefijo {d.Prefijo}   longitud {d.Longitud}" +
                              $"   costumbre {r.Costumbre}");
        }

        return 0;
    }

    private static int Olvidar()
    {
        bool habia = Uso.Olvidar();
        Console.WriteLine(habia
            ? $"[lanzador] borrado {Uso.Ruta}"
            : $"[lanzador] no habia nada que borrar en {Uso.Ruta}");
        return 0;
    }

    /// <summary>El nombre con las letras que coincidieron entre corchetes.</summary>
    private static string Marcado(string nombre, int[] donde)
    {
        HashSet<int> puestos = new(donde);
        StringBuilder sb = new(nombre.Length * 2);
        for (int i = 0; i < nombre.Length; i++)
        {
            if (puestos.Contains(i)) sb.Append('[').Append(nombre[i]).Append(']');
            else sb.Append(nombre[i]);
        }
        return sb.ToString();
    }
}
