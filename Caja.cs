namespace Lanzador;

/// <summary>
/// El texto que escribes: el bufer, el cursor y la seleccion. <b>Sin nada de Win32</b>, a
/// proposito: asi la parte que de verdad tiene casos limite —borrar con seleccion, mover
/// por palabras, pegar encima de lo seleccionado— se comprueba entera desde
/// <c>--check</c>, sin abrir una ventana ni mandar teclas a nadie.
/// <para>
/// Esto existe porque la caja dejo de ser un control <c>EDIT</c> del sistema
/// (SEGURIDAD.md §3.2): un EDIT pinta su fondo opaco y no puede ser translucido. Todo lo
/// que Windows hacia por nosotros esta aqui.
/// </para>
/// </summary>
internal sealed class Caja
{
    private string _texto = string.Empty;

    /// <summary>Lo escrito.</summary>
    public string Texto => _texto;

    /// <summary>Donde esta el cursor, entre 0 y la longitud.</summary>
    public int Cursor { get; private set; }

    /// <summary>
    /// El otro extremo de la seleccion. Cuando coincide con el cursor no hay seleccion.
    /// Se lleva un ancla y no un "desde/hasta" porque al seleccionar con May+flechas el
    /// que se mueve es el cursor y el ancla se queda donde empezaste.
    /// </summary>
    public int Ancla { get; private set; }

    public bool HaySeleccion => Cursor != Ancla;
    public int Desde => Math.Min(Cursor, Ancla);
    public int Hasta => Math.Max(Cursor, Ancla);

    public void Vaciar()
    {
        _texto = string.Empty;
        Cursor = 0;
        Ancla = 0;
    }

    /// <summary>Una letra. Si habia seleccion, la sustituye.</summary>
    public void Escribir(char c)
    {
        BorrarSeleccion();
        _texto = _texto.Insert(Cursor, c.ToString());
        Cursor++;
        Ancla = Cursor;
    }

    /// <summary>
    /// Pegar. Se limpian los saltos de linea: la caja es de una sola linea, y pegar una
    /// ruta copiada de un editor suele traerse el salto del final.
    /// </summary>
    public void Pegar(string texto)
    {
        // El salto de Windows PRIMERO: cambiando \r y \n por separado, un \r\n dejaba dos
        // espacios donde tenia que haber uno. Lo encontro --check.
        string limpio = texto.Replace("\r\n", " ").Replace("\r", " ").Replace("\n", " ").Trim();
        if (limpio.Length == 0) return;

        BorrarSeleccion();
        _texto = _texto.Insert(Cursor, limpio);
        Cursor += limpio.Length;
        Ancla = Cursor;
    }

    /// <summary>
    /// Retroceso o Suprimir. Con seleccion, borra la seleccion y ya — que es lo que
    /// espera cualquiera y lo que mas se olvida al escribir esto a mano.
    /// </summary>
    public void Borrar(bool haciaAtras, bool palabra)
    {
        if (HaySeleccion) { BorrarSeleccion(); return; }

        if (haciaAtras)
        {
            if (Cursor == 0) return;
            int desde = palabra ? SaltoDePalabra(-1) : Cursor - 1;
            _texto = _texto.Remove(desde, Cursor - desde);
            Cursor = desde;
        }
        else
        {
            if (Cursor >= _texto.Length) return;
            int hasta = palabra ? SaltoDePalabra(+1) : Cursor + 1;
            _texto = _texto.Remove(Cursor, hasta - Cursor);
        }

        Ancla = Cursor;
    }

    /// <summary>
    /// Mueve el cursor. Sin May y con seleccion puesta, la primera pulsacion <b>deshace la
    /// seleccion</b> y deja el cursor en el extremo hacia el que ibas, en vez de moverse
    /// una letra desde donde estaba.
    /// </summary>
    public void Mover(int direccion, bool seleccionando, bool palabra)
    {
        if (!seleccionando && HaySeleccion && !palabra)
        {
            Cursor = direccion < 0 ? Desde : Hasta;
            Ancla = Cursor;
            return;
        }

        Cursor = palabra
            ? SaltoDePalabra(direccion)
            : Math.Clamp(Cursor + direccion, 0, _texto.Length);

        if (!seleccionando) Ancla = Cursor;
    }

    public void AlBorde(int direccion, bool seleccionando)
    {
        Cursor = direccion < 0 ? 0 : _texto.Length;
        if (!seleccionando) Ancla = Cursor;
    }

    /// <summary>Ctrl+A.</summary>
    public void Todo()
    {
        Ancla = 0;
        Cursor = _texto.Length;
    }

    /// <summary>Del raton: pone el cursor donde se pincho, o arrastra la seleccion.</summary>
    public void Poner(int posicion, bool arrastrando)
    {
        Cursor = Math.Clamp(posicion, 0, _texto.Length);
        if (!arrastrando) Ancla = Cursor;
    }

    private void BorrarSeleccion()
    {
        if (!HaySeleccion) return;

        int desde = Desde;
        _texto = _texto.Remove(desde, Hasta - desde);
        Cursor = desde;
        Ancla = desde;
    }

    /// <summary>
    /// El siguiente salto de palabra en esa direccion. Primero se come los espacios y
    /// luego lo que no es espacio, que es como se comporta cualquier caja de texto.
    /// </summary>
    private int SaltoDePalabra(int direccion)
    {
        int i = Cursor;

        if (direccion < 0)
        {
            while (i > 0 && _texto[i - 1] == ' ') i--;
            while (i > 0 && _texto[i - 1] != ' ') i--;
            return i;
        }

        while (i < _texto.Length && _texto[i] == ' ') i++;
        while (i < _texto.Length && _texto[i] != ' ') i++;
        return i;
    }
}
