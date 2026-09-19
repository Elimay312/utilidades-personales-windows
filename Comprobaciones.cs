using Windows.Win32;

namespace Renombrar;

/// <summary>
/// <c>--check</c>. Comprueba el motor y la vista previa <b>sin tocar el disco</b>: todo lo
/// que hay aqui son nombres inventados y fechas fijas.
///
/// <para>
/// Inventados a proposito. Si los casos dependieran de una carpeta de esta maquina,
/// dejarian de comprobar nada en cuanto alguien moviese un fichero, y ademas la
/// comprobacion tendria que escribir, que es justo lo que SEGURIDAD.md §5.1 no quiere que
/// haga falta.
/// </para>
/// </summary>
internal static class Comprobaciones
{
    private static readonly DateTime Creado = new(2024, 3, 12, 9, 30, 0);
    private static readonly DateTime Tocado = new(2025, 11, 2, 18, 0, 0);

    /// <summary>El motor: nombre de entrada, reglas, lo que tiene que salir, y por que ese caso esta aqui.</summary>
    private static readonly (string Entra, Regla[] Reglas, string Sale, string Porque)[] CasosDelMotor =
    [
        ("factura.pdf", [new Regla(Tipo.Plantilla, "Recibo_{fecha}_{n:000}", Desde: 1)],
         "Recibo_2024-03-12_001.pdf", "la plantilla tipica de la oficina, con fecha y numero"),

        ("factura.pdf", [new Regla(Tipo.Plantilla, "{nombre}_pagada")],
         "factura_pagada.pdf", "{nombre} conserva lo que habia"),

        ("factura.pdf", [new Regla(Tipo.Plantilla, "{fecha:yyyy-MM}", Modificacion: true)],
         "2025-11.pdf", "la ficha acepta formato y puede mirar la fecha de modificacion"),

        ("IMG_0042.jpg", [new Regla(Tipo.Reemplazar, "img_", "foto_")],
         "foto_0042.jpg", "buscar no distingue mayusculas de minusculas"),

        ("20240312 acme.pdf", [new Regla(Tipo.Reemplazar, @"^(\d{4})(\d{2})(\d{2}) ", "$1-$2-$3_", Regex: true)],
         "2024-03-12_acme.pdf", "la expresion regular usa sus grupos en el reemplazo"),

        ("factura_copia.pdf", [new Regla(Tipo.Quitar, Desde: -6, Cuenta: 6)],
         "factura.pdf", "quitar desde el final cuenta hacia atras"),

        ("factura.pdf", [new Regla(Tipo.Quitar, Desde: 0, Cuenta: 99)],
         ".pdf", "quitar mas de lo que hay no revienta, deja el nombre vacio y la previa lo marca"),

        ("el niño.txt", [new Regla(Tipo.Mayusculas)],
         "EL NIÑO.txt", "la enie sube a mayusculas: es lo que se pierde sin ICU"),

        ("RECIBO ACME S.L..pdf", [new Regla(Tipo.Titulo)],
         "Recibo Acme S.L..pdf", "titulo baja antes de subir, si no lo que esta a gritos se queda a gritos"),

        ("notas.TXT", [new Regla(Tipo.Extension, "txt")],
         "notas.txt", "la extension admite escribirse sin punto"),

        ("IMG_0042.jpg", [new Regla(Tipo.Reemplazar, "IMG_", "acme_"), new Regla(Tipo.Mayusculas)],
         "ACME_0042.jpg", "las reglas se encadenan: la segunda trabaja sobre lo que dejo la primera"),

        ("factura.pdf", [new Regla(Tipo.Plantilla, "Recibo")],
         "Recibo.pdf", "la plantilla no toca la extension"),
    ];

    internal static int Ejecutar()
    {
        int fallos = 0;

        Console.WriteLine("Motor — una regla entra, un nombre sale");
        foreach ((string entra, Regla[] reglas, string esperado, string porque) in CasosDelMotor)
        {
            string sale = Pasar(entra, reglas, 0);
            bool bien = sale == esperado;
            if (!bien) fallos++;
            Console.WriteLine($"  {(bien ? "ok  " : "FALLA")} {entra,-22} -> {sale,-28} {porque}");
            if (!bien) Console.WriteLine($"         esperaba {esperado}");
        }

        Console.WriteLine();
        Console.WriteLine("Motor — la numeracion sigue el orden de la lista");
        fallos += Exige("el segundo fichero recibe el 2",
                        Pasar("b.txt", [new Regla(Tipo.Plantilla, "{n}", Desde: 1)], 1) == "2.txt");
        fallos += Exige("y con Desde 0 empieza en 0",
                        Pasar("a.txt", [new Regla(Tipo.Plantilla, "{n}")], 0) == "0.txt");
        fallos += Exige("foto2 va antes que foto10, como en el Explorador",
                        PInvoke.StrCmpLogical("foto2.jpg", "foto10.jpg") < 0);
        fallos += Exige("y no es el orden alfabetico, que los pondria al reves",
                        string.CompareOrdinal("foto2.jpg", "foto10.jpg") > 0);

        Console.WriteLine();
        Console.WriteLine("Previa — nombres que Windows no acepta");
        fallos += Exige("interrogante",        Previa.Invalido("fact?ura") is not null);
        fallos += Exige("barra",               Previa.Invalido("2024/03") is not null);
        fallos += Exige("dos puntos",          Previa.Invalido("acme: s.l.") is not null);
        fallos += Exige("termina en punto",    Previa.Invalido("factura.") is not null);
        fallos += Exige("termina en espacio",  Previa.Invalido("factura ") is not null);
        fallos += Exige("vacio",               Previa.Invalido("") is not null);
        fallos += Exige("solo espacios",       Previa.Invalido("   ") is not null);
        fallos += Exige("CON es reservado",    Previa.Invalido("CON") is not null);
        fallos += Exige("y CON.txt tambien",   Previa.Invalido("CON.txt") is not null);
        fallos += Exige("pero CONTRATO no",    Previa.Invalido("CONTRATO.txt") is null);
        fallos += Exige("un nombre normal pasa", Previa.Invalido("Recibo_2024-03-12_001.pdf") is null);

        Console.WriteLine();
        Console.WriteLine("Previa — los estados de la tabla");

        fallos += Exige("sin reglas, todo sin cambio",
                        Estados(["a.txt", "b.txt"], []) is [Estado.SinCambio, Estado.SinCambio]);

        fallos += Exige("dos ficheros al mismo destino son colision",
                        Estados(["a.txt", "b.txt"], [new Regla(Tipo.Plantilla, "igual")])
                        is [Estado.Colision, Estado.Colision]);

        fallos += Exige("un destino que ya existe en la carpeta se marca",
                        Estados(["a.txt"], [new Regla(Tipo.Plantilla, "ocupado")], ["a.txt", "ocupado.txt"])
                        is [Estado.YaExiste]);

        // El caso que hizo cambiar el codigo: 2.txt esta en el lote, pero no se mueve, asi
        // que su nombre NO queda libre. Mirando solo "esta en el lote" esto salia Ok y el
        // lote petaba a mitad al aplicarlo.
        fallos += Exige("un destino del lote que NO se mueve sigue estando ocupado",
                        Estados(["1.txt", "2.txt"], [new Regla(Tipo.Reemplazar, "^1$", "2", Regex: true)],
                                ["1.txt", "2.txt"])
                        is [Estado.YaExiste, Estado.SinCambio]);

        // Y el contrario, que es el que justifica que exista el temporal en Aplicar.
        fallos += Exige("intercambiar dos nombres si vale",
                        Estados(["1.txt", "2.txt"], [new Regla(Tipo.Reemplazar, @"^(1|2)$", "$1$1", Regex: true)],
                                ["1.txt", "2.txt"])
                        is [Estado.Ok, Estado.Ok]);

        fallos += Exige("cambiar solo mayusculas es un cambio, no 'sin cambio'",
                        Estados(["foto.txt"], [new Regla(Tipo.Mayusculas)], ["foto.txt"])
                        is [Estado.Ok]);

        fallos += Exige("un nombre imposible se marca antes que nada",
                        Estados(["a.txt"], [new Regla(Tipo.Plantilla, "CON")]) is [Estado.Invalido]);

        // Windows deja crear algunos nombres que luego el Explorador no sabe tocar, asi
        // que una carpeta puede tener dentro un CON.pdf de verdad. Si el lote no lo toca,
        // la fila no tiene por que salir en rojo.
        fallos += Exige("un nombre que ya estaba mal y no cambia sale como sin cambio",
                        Estados(["CON.pdf"], []) is [Estado.SinCambio]);

        fallos += Exige("una expresion regular a medias no mata la previa",
                        Estados(["a.txt"], [new Regla(Tipo.Reemplazar, "(sin cerrar", "x", Regex: true)])
                        is [Estado.Invalido]);

        fallos += Exige("una ruta que se pasa de 260 se marca",
                        Previa.DemasiadoLarga(@"C:\recibos\a.txt", new string('x', 300)));
        fallos += Exige("pero una que ya estaba larga no es culpa del renombrado",
                        !Previa.DemasiadoLarga(@"C:\" + new string('x', 300) + @"\a.txt", "b.txt"));

        Console.WriteLine();
        if (fallos == 0) { Console.WriteLine("TODO BIEN"); return 0; }
        Console.WriteLine($"{fallos} comprobacion(es) fallan");
        return 1;
    }

    private static string Pasar(string nombre, Regla[] reglas, int indice)
    {
        Trozos t = new(Path.GetFileNameWithoutExtension(nombre), Path.GetExtension(nombre));
        Datos d = new(indice, Creado, Tocado);
        foreach (Regla r in reglas) t = r.Aplicar(t, d);
        return t.ToString();
    }

    /// <summary>Los estados que da la previa para esos nombres. <c>ocupados</c> vacio significa "la carpeta solo tiene estos".</summary>
    private static Estado[] Estados(string[] nombres, Regla[] reglas, string[]? ocupados = null)
    {
        List<Fichero> ficheros = [.. nombres.Select(n => new Fichero(@"C:\recibos\" + n, n, Creado, Tocado))];
        HashSet<string> hay = new(ocupados ?? nombres, StringComparer.OrdinalIgnoreCase);
        return [.. Previa.Calcular(ficheros, reglas, hay).Select(f => f.Estado)];
    }

    private static int Exige(string que, bool secumple)
    {
        Console.WriteLine($"  {(secumple ? "ok  " : "FALLA")} {que}");
        return secumple ? 0 : 1;
    }
}
