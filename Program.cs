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
            case "--iconos":  return MedirIconos();
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

        Stopwatch reloj = Stopwatch.StartNew();

        LanzadorConfig config = Config.Cargar();
        Config.AplicarAutoArranque(config.AutoArranque);
        Uso uso = Uso.Cargar();

        // La ventana y el atajo PRIMERO, el indice despues y en otro hilo. Antes se
        // indexaba antes de crear nada, y el atajo no existia durante el primer segundo
        // largo: con autoarranque eso cae justo en el inicio de sesion, que es cuando mas
        // lento va todo. Pulsabas Alt+Espacio y no pasaba nada, sin forma de saber por que.
        LanzadorWindow? ventana = LanzadorWindow.Crear(config, uso);
        if (ventana is null)
        {
            Console.Error.WriteLine("[lanzador] no se pudo crear la ventana.");
            return 2;
        }

        Console.WriteLine($"[lanzador] atajo listo en {reloj.ElapsedMilliseconds} ms; indexando...");
        Indice.EnSegundoPlano(indice =>
        {
            ventana.RecibirIndice(indice);
            Console.WriteLine($"[lanzador] {indice.Count} entradas en el indice, " +
                              $"a los {reloj.ElapsedMilliseconds} ms.");
        });

        Precalentar(uso);

        LanzadorWindow.Bucle();
        ventana.Dispose();
        return 0;
    }

    /// <summary>Cuantos iconos se dejan hechos al arrancar, de lo mas reciente.</summary>
    private const int Precalentados = 30;

    /// <summary>
    /// Deja hechos, nada mas arrancar, los iconos de lo que sueles abrir. Como salen en
    /// la primera fila casi siempre, en la practica el icono ya esta antes de que acabes
    /// de escribir.
    /// <para>
    /// Se recorre <c>uso.json</c> y no el indice: lo que abres puede ser una carpeta o un
    /// fichero que encontro Everything, y esos no estan en el indice. Recorriendo el
    /// indice se saltaban justo los que mas usas.
    /// </para>
    /// <para>
    /// <b>Solo los que usas, y como mucho 30.</b> Sacar los 239 del indice cuesta 2,2 s y
    /// deja el proceso en ~125 MB, porque el shell carga en nuestro proceso el manejador
    /// de iconos de cada aplicacion y ya no lo descarga. No hay ningun caso nuevo de
    /// lectura: son los mismos iconos que se sacarian al buscarlos, solo que antes.
    /// </para>
    /// </summary>
    private static void Precalentar(Uso uso)
    {
        List<string> recientes = uso.Lanzamientos
            .OrderByDescending(l => l.Value.Ultimo)
            .Take(Precalentados)
            .Select(l => l.Key)
            .ToList();

        if (recientes.Count == 0) return;

        foreach (string destino in recientes) Iconos.Pedir(destino);
        Console.WriteLine($"[lanzador] precalentando {recientes.Count} iconos de los que usas.");
    }

    private static int Ayuda()
    {
        Console.WriteLine("""
            lanzador [modo]

              --check          comprueba el algoritmo, el decaimiento y la calculadora
              --indice         vuelca las aplicaciones encontradas y cuanto costo
              --buscar TEXTO   los mejores resultados, con su puntuacion desglosada
              --iconos         cuanto cuesta tener todos los iconos en memoria
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

        // --- El reparto de bytes de Everything (H5) ----------------------------------
        // Es el codigo con mas riesgo del proyecto: un campo mal alineado no da un error,
        // da basura, y eso se tarda mucho mas en ver. Se le da una respuesta armada a
        // mano, con el mismo reparto que dice ipc/everything_ipc.h.
        Console.WriteLine();
        Console.WriteLine("Everything — deshacer una respuesta");
        fallos += ComprobarEverything();

        // --- La calculadora y los prefijos web (H6) ----------------------------------
        Console.WriteLine();
        Console.WriteLine("Cuentas — lo que sale");
        foreach ((string expr, double esperado) in new (string, double)[]
                 {
                     ("2+2", 4),
                     ("2+2*7", 16),            // precedencia, no 28
                     ("(2+2)*7", 28),
                     ("10/4", 2.5),
                     ("10:4", 2.5),            // los dos por si acaso
                     ("2^3^2", 512),           // asociativa por la derecha, no 64
                     ("-3+5", 2),
                     ("2 * -3", -6),
                     ("1,5+1,5", 3),           // la coma decimal de aqui
                     ("1.5+1.5", 3),
                     ("3x4", 12),
                     ("100-(20+30)", 50),
                 })
        {
            double? sale = Cuentas.Evaluar(expr);
            bool bien = sale is not null && Math.Abs(sale.Value - esperado) < 1e-9;
            if (!bien) fallos++;
            Console.WriteLine($"  {(bien ? "ok  " : "FALLA")} {expr,-14} = {(sale is null ? "(nada)" : Cuentas.Escribir(sale.Value))}" +
                              (bien ? "" : $"   esperaba {esperado}"));
        }

        Console.WriteLine();
        Console.WriteLine("Cuentas — lo que NO es una cuenta");
        foreach (string no in new[]
                 {
                     "5",            // un numero suelto no es una cuenta
                     "brave",
                     "a+b",
                     "1/0",          // infinito no informa de nada
                     "2+",
                     "(2+3",
                     "2+3)",
                     "10%3",         // el % no existe a proposito (SEGURIDAD.md §4)
                     "g gatos",      // esto es un prefijo web, no una cuenta
                 })
        {
            bool bien = Cuentas.Evaluar(no) is null;
            if (!bien) fallos++;
            Console.WriteLine($"  {(bien ? "ok  " : "FALLA")} \"{no}\"" +
                              (bien ? "" : $"   pero devolvio {Cuentas.Evaluar(no)}"));
        }

        Console.WriteLine();
        Console.WriteLine("Web — la plantilla y su esquema");
        Dictionary<string, string> plantillas = new(StringComparer.OrdinalIgnoreCase)
        {
            ["g"] = "https://www.google.com/search?q={}",
            ["mal"] = "file:///C:/{}",
            ["raro"] = "loquesea:{}",
        };

        Entrada? buscado = Proveedores.Especial("g gatos con sombrero", plantillas);
        fallos += Exige("el prefijo construye la URL",
                        buscado?.Destino == "https://www.google.com/search?q=gatos%20con%20sombrero");
        fallos += Exige("y el termino va escapado",
                        buscado is not null && !buscado.Destino.Contains(' '));
        fallos += Exige("una plantilla file: no se abre", Proveedores.Especial("mal cosa", plantillas) is null);
        fallos += Exige("un esquema inventado tampoco", Proveedores.Especial("raro cosa", plantillas) is null);
        fallos += Exige("un prefijo que no existe no hace nada",
                        Proveedores.Especial("zz cosa", plantillas) is null);
        fallos += Exige("un prefijo sin termino no hace nada",
                        Proveedores.Especial("g ", plantillas) is null);
        fallos += Exige("la cuenta gana al prefijo web",
                        Proveedores.Especial("2+2", plantillas)?.SoloSeMira == true);

        // --- Los iconos (H7) ----------------------------------------------------------
        // Heredadas de dock/Icons.cs junto con el codigo: las dos cosas que comprueban son
        // fallos que no se ven mirando, solo se notan como "los iconos se ven raros".
        Console.WriteLine();
        Console.WriteLine("Iconos — premultiplicado");

        // Canales por encima del alfa: NO estaba premultiplicado. Sin premultiplicar,
        // Composition deja halos negros en los bordes.
        byte[] crudo = [200, 100, 50, 128];
        Iconos.Premultiplicar(crudo);
        fallos += Exige("se premultiplica lo que no lo estaba",
                        crudo[0] == 200 * 128 / 255 && crudo[1] == 100 * 128 / 255
                        && crudo[2] == 50 * 128 / 255 && crudo[3] == 128);

        // Y no dos veces, que oscureceria el icono.
        byte[] yaEsta = [50, 40, 30, 128];
        byte[] copia = (byte[])yaEsta.Clone();
        Iconos.Premultiplicar(yaEsta);
        fallos += Exige("y no se premultiplica dos veces", yaEsta.AsSpan().SequenceEqual(copia));

        byte[] opaco = [10, 20, 30, 255];
        Iconos.Premultiplicar(opaco);
        fallos += Exige("un pixel opaco no se toca", opaco[0] == 10 && opaco[1] == 20 && opaco[2] == 30);

        Console.WriteLine();
        Console.WriteLine("Iconos — caja del dibujo");
        fallos += Exige("un bloque de 3x5 mide 5",
                        Iconos.Lado(Lienzo(8, (x, y) => x is >= 2 and <= 4 && y is >= 1 and <= 5 ? (byte)255 : (byte)0)) == 5);
        fallos += Exige("un lienzo lleno ocupa el lienzo", Iconos.Lado(Lienzo(8, (_, _) => 255)) == 8);
        fallos += Exige("un lienzo vacio no ocupa nada", Iconos.Lado(Lienzo(8, (_, _) => 0)) == 0);

        // El marco que el shell pinta alrededor de una miniatura viene con alfa 38. Si
        // contara, la caja saldria siempre del lienzo entero y nunca se pediria el icono
        // mas pequeno: los iconos viejos se verian como sellos diminutos.
        fallos += Exige("el marco de alfa 38 no cuenta como dibujo",
                        Iconos.Lado(Lienzo(8, (x, y) => x == 0 || y == 0 || x == 7 || y == 7 ? (byte)38 : (byte)0)) == 0);

        // --- La caja de texto (H9) -----------------------------------------------------
        // Lo que antes hacia el control EDIT del sistema y ahora hacemos nosotros. Son
        // casos que no se ven mirando la pantalla: se nota al usarla y ya es tarde.
        Console.WriteLine();
        Console.WriteLine("Caja — escribir y borrar");
        Caja c = new();
        foreach (char l in "hola") c.Escribir(l);
        fallos += Exige("escribir deja el cursor al final", c.Texto == "hola" && c.Cursor == 4);

        c.Mover(-1, seleccionando: false, palabra: false);
        c.Escribir('X');
        fallos += Exige("escribir en medio inserta donde esta el cursor", c.Texto == "holXa");

        c.Borrar(haciaAtras: true, palabra: false);
        fallos += Exige("retroceso quita la de la izquierda", c.Texto == "hola" && c.Cursor == 3);

        c.Borrar(haciaAtras: false, palabra: false);
        fallos += Exige("suprimir quita la de la derecha", c.Texto == "hol" && c.Cursor == 3);

        Caja v = new();
        v.Borrar(haciaAtras: true, palabra: false);
        v.Borrar(haciaAtras: false, palabra: false);
        fallos += Exige("borrar en una caja vacia no revienta", v.Texto.Length == 0 && v.Cursor == 0);

        Console.WriteLine();
        Console.WriteLine("Caja — seleccion");
        Caja s = new();
        foreach (char l in "uno dos tres") s.Escribir(l);

        s.Todo();
        fallos += Exige("Ctrl+A coge todo", s.Desde == 0 && s.Hasta == 12);

        s.Escribir('z');
        fallos += Exige("escribir con seleccion la sustituye", s.Texto == "z" && !s.HaySeleccion);

        Caja s2 = new();
        foreach (char l in "abcdef") s2.Escribir(l);
        s2.Mover(-1, seleccionando: true, palabra: false);
        s2.Mover(-1, seleccionando: true, palabra: false);
        fallos += Exige("Mayus+izquierda selecciona hacia atras", s2.Desde == 4 && s2.Hasta == 6);

        s2.Borrar(haciaAtras: true, palabra: false);
        fallos += Exige("retroceso con seleccion borra la seleccion, no una letra", s2.Texto == "abcd");

        // El caso que mas se olvida: con seleccion puesta, una flecha SIN Mayus no mueve
        // una letra desde donde estaba el cursor, deshace la seleccion y salta al extremo.
        Caja s3 = new();
        foreach (char l in "abcdef") s3.Escribir(l);
        s3.Todo();
        s3.Mover(-1, seleccionando: false, palabra: false);
        fallos += Exige("flecha sin Mayus deshace la seleccion y va al extremo",
                        s3.Cursor == 0 && !s3.HaySeleccion);

        Console.WriteLine();
        Console.WriteLine("Caja — por palabras y pegar");
        Caja pal = new();
        foreach (char l in "uno dos tres") pal.Escribir(l);
        pal.Mover(-1, seleccionando: false, palabra: true);
        fallos += Exige("Ctrl+izquierda salta al principio de la palabra", pal.Cursor == 8);

        pal.Mover(-1, seleccionando: false, palabra: true);
        fallos += Exige("y otra vez, a la anterior", pal.Cursor == 4);

        pal.AlBorde(+1, seleccionando: false);
        pal.Borrar(haciaAtras: true, palabra: true);
        fallos += Exige("Ctrl+retroceso borra la palabra entera", pal.Texto == "uno dos ");

        Caja g = new();
        foreach (char l in "abc") g.Escribir(l);
        g.Mover(-1, seleccionando: true, palabra: false);
        g.Pegar("  ruta con espacios  ");
        fallos += Exige("pegar sustituye la seleccion y se recorta",
                        g.Texto == "abruta con espacios");

        Caja n = new();
        n.Pegar("dos\r\nlineas");
        fallos += Exige("pegar varias lineas las junta en una",
                        n.Texto == "dos lineas" && !n.Texto.Contains('\n'));

        Caja z = new();
        foreach (char l in "algo") z.Escribir(l);
        z.Vaciar();
        fallos += Exige("vaciar deja la caja como nueva",
                        z.Texto.Length == 0 && z.Cursor == 0 && !z.HaySeleccion);

        Console.WriteLine();
        if (fallos == 0) { Console.WriteLine("TODO BIEN"); return 0; }
        Console.WriteLine($"{fallos} comprobacion(es) fallan");
        return 1;
    }

    /// <summary>
    /// Arma un EVERYTHING_IPC_LISTW a mano y comprueba que se deshace bien, incluidos los
    /// casos en que la respuesta miente sobre su propio tamano.
    /// </summary>
    private static unsafe int ComprobarEverything()
    {
        const int Cabecera = 28;
        const int TamItem = 12;

        // Dos resultados: un fichero en una carpeta y una carpeta raiz sin ruta.
        string[] nombres = ["notas.txt", "Proyectos"];
        string[] rutas = [@"C:\Users\yo\Documentos", ""];
        uint[] marcas = [0, 1];   // 1 = EVERYTHING_IPC_FOLDER

        int textos = Cabecera + nombres.Length * TamItem;
        int total = textos;
        foreach (string s in nombres.Concat(rutas)) total += (s.Length + 1) * 2;

        byte[] buffer = new byte[total];
        int fallos = 0;

        fixed (byte* p = buffer)
        {
            uint* cab = (uint*)p;
            cab[0] = 1; cab[1] = 1; cab[2] = 2;   // totfolders, totfiles, totitems
            cab[3] = 1; cab[4] = 1; cab[5] = 2;   // numfolders, numfiles, numitems
            cab[6] = 0;                           // offset

            int escribiendo = textos;
            for (int i = 0; i < nombres.Length; i++)
            {
                uint* item = (uint*)(p + Cabecera + i * TamItem);
                item[0] = marcas[i];

                item[1] = (uint)escribiendo;
                escribiendo += Poner(p, escribiendo, nombres[i]);

                item[2] = (uint)escribiendo;
                escribiendo += Poner(p, escribiendo, rutas[i]);
            }

            List<Entrada> leidos = Everything.Leer(p, (uint)total);

            fallos += Exige("salen los dos resultados", leidos.Count == 2);
            if (leidos.Count == 2)
            {
                fallos += Exige("nombre y ruta se juntan bien",
                                leidos[0].Destino == @"C:\Users\yo\Documentos\notas.txt");
                fallos += Exige("sin ruta, el destino es el nombre a secas",
                                leidos[1].Destino == "Proyectos");
                fallos += Exige("la marca de carpeta se lee",
                                !leidos[0].EsCarpetaDeDisco && leidos[1].EsCarpetaDeDisco);
                fallos += Exige("vienen marcados como fichero, no como aplicacion",
                                leidos.TrueForAll(e => e.EsFichero));
            }

            // Y los casos en que la respuesta miente. Un numitems que no cabe en el sobre
            // significaria leer memoria que no es nuestra.
            cab[5] = 100000;
            fallos += Exige("un numitems imposible no se lee", Everything.Leer(p, (uint)total).Count == 0);
            cab[5] = 2;

            uint* primero = (uint*)(p + Cabecera);
            uint bueno = primero[1];
            primero[1] = (uint)total + 500;
            fallos += Exige("un offset fuera del sobre salta ese item",
                            Everything.Leer(p, (uint)total).Count == 1);
            primero[1] = bueno;

            fallos += Exige("un sobre vacio no revienta", Everything.Leer(p, 4).Count == 0);
            fallos += Exige("un puntero nulo no revienta", Everything.Leer(null, 999).Count == 0);
        }

        return fallos;
    }

    /// <summary>Copia una cadena terminada en cero y devuelve cuantos bytes ocupo.</summary>
    private static unsafe int Poner(byte* baseP, int en, string s)
    {
        char* destino = (char*)(baseP + en);
        s.AsSpan().CopyTo(new Span<char>(destino, s.Length));
        destino[s.Length] = '\0';
        return (s.Length + 1) * 2;
    }

    /// <summary>Un lienzo cuadrado con el alfa que diga <paramref name="alfa"/>.</summary>
    private static Icono Lienzo(int lado, Func<int, int, byte> alfa)
    {
        byte[] bgra = new byte[lado * lado * 4];
        for (int y = 0; y < lado; y++)
        {
            for (int x = 0; x < lado; x++) bgra[((y * lado) + x) * 4 + 3] = alfa(x, y);
        }

        return new Icono(lado, lado, bgra);
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

        List<Entrada> indice = Indice.Construir();
        int enBruto = appsFolder.Count + soloEnMenus;

        Console.WriteLine($"shell:AppsFolder   {appsFolder.Count,5}   {msFolder,4} ms");
        Console.WriteLine($"menus Inicio .lnk  {menus.Count,5}   {msMenus,4} ms   ({soloEnMenus} no estan en AppsFolder)");
        Console.WriteLine($"sitios del sistema {Proveedores.Sistema().Count(),5}");
        Console.WriteLine($"relleno quitado    {enBruto + Proveedores.Sistema().Count() - indice.Count,5}   documentos y desinstaladores");
        Console.WriteLine($"indice             {indice.Count,5}   {msFolder + msMenus,4} ms");
        Console.WriteLine();
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

    /// <summary>
    /// Cuanto cuesta tener TODOS los iconos del indice en memoria. Contesta la unica
    /// pregunta que decide si precargarlos al arrancar compensa: si son 200 ms y 4 MB,
    /// si; si son diez segundos y 200 MB, no.
    /// </summary>
    private static int MedirIconos()
    {
        List<Entrada> indice = Indice.Construir();
        using Process yo = Process.GetCurrentProcess();
        long antes = yo.WorkingSet64;

        Stopwatch reloj = Stopwatch.StartNew();
        using ManualResetEventSlim listo = new(false);
        int faltan = indice.Count;

        Iconos.Arrancar(() => { });
        foreach (Entrada e in indice) Iconos.Pedir(e.Destino);

        // Se espera contando los PROCESADOS, no los guardados: Pedir reserva el hueco
        // con un null al instante, asi que contar los guardados daba 261 en 2 ms y la
        // medicion decia que los iconos eran gratis. Otra sonda que media otra cosa.
        while (Iconos.Procesados < indice.Count && reloj.ElapsedMilliseconds < 120000)
        {
            Thread.Sleep(50);
        }

        long ms = reloj.ElapsedMilliseconds;
        (int cuantos, long bytes) = Iconos.Cuenta();
        yo.Refresh();

        int conIcono = 0;
        foreach (Entrada e in indice) { if (Iconos.Hay(e.Destino) is not null) conIcono++; }
        Console.WriteLine($"{indice.Count} entradas, {conIcono} con icono ({cuantos} pedidos)");
        Console.WriteLine($"  tiempo    {ms} ms   ({(double)ms / Math.Max(1, cuantos):0.0} ms por icono)");
        Console.WriteLine($"  guardados {bytes / 1048576.0:0.0} MB");
        Console.WriteLine($"  trabajo   {antes / 1048576.0:0} MB -> {yo.WorkingSet64 / 1048576.0:0} MB");
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
