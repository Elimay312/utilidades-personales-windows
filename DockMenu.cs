using System.Numerics;
using Windows.Graphics.DirectX;
using Windows.UI;
using Windows.UI.Composition;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct2D.Common;
using Windows.Win32.System.WinRT;
using Windows.Win32.System.WinRT.Composition;
using WinRT;

namespace Dock;

/// <summary>
/// El menú del clic derecho, dibujado en el compositor.
///
/// No se usa <c>TrackPopupMenu</c> a propósito. Un menú de Windows exige que su ventana
/// tenga el foco para cerrarse bien, y que el dock <b>nunca</b> robe el foco es uno de
/// los criterios de aceptación del proyecto: es lo que hace que puedas clicarlo sin
/// perder lo que estabas escribiendo. Dibujarlo nosotros sale más barato que hacer una
/// excepción a eso, sobre todo ahora que ya hay texto (ver <see cref="Labels"/>).
///
/// <para>
/// Cabe en el hueco que hay encima de la barra, el mismo que se reservó para las
/// etiquetas. Por eso son dos entradas y no diez: si algún día hacen falta más, habrá
/// que crecer la ventana, y eso toca las expresiones que llevan su alto horneado.
/// </para>
/// </summary>
internal sealed unsafe class DockMenu(Compositor compositor, ContainerVisual parent, CompositionGraphicsDevice graphics)
{
    private const float RowHeight = 30f;
    private const float PaddingX = 12f;
    private const float PaddingY = 5f;

    private readonly Compositor _compositor = compositor;
    private readonly ContainerVisual _parent = parent;
    private readonly CompositionGraphicsDevice _graphics = graphics;

    private ContainerVisual? _menu;
    private SpriteVisual? _hot;
    private string[] _items = [];
    private float _scale = 1f;
    private Vector2 _origin;
    private Vector2 _size;
    private int _hotIndex = -1;

    public bool IsOpen => _menu is not null;

    /// <summary>Qué entrada eligió el usuario, o -1 si clicó fuera.</summary>
    public int HitTest(float x, float y)
    {
        if (_menu is null) return -1;

        float local = y - _origin.Y;
        if (x < _origin.X || x > _origin.X + _size.X || local < 0f || local > _size.Y) return -1;

        int row = (int)((local - PaddingY * _scale) / (RowHeight * _scale));
        return row >= 0 && row < _items.Length ? row : -1;
    }

    /// <summary>Resalta la entrada bajo el cursor.</summary>
    public void SetHot(int index)
    {
        if (_hot is null || index == _hotIndex) return;

        _hotIndex = index;
        _hot.Opacity = index < 0 ? 0f : 1f;

        if (index >= 0)
        {
            _hot.Offset = new Vector3(
                PaddingY * _scale,
                PaddingY * _scale + index * RowHeight * _scale,
                0f);
        }
    }

    /// <summary>
    /// Abre el menú encima del punto dado, sin salirse de los límites que se le pasan.
    /// </summary>
    public void Open(string[] items, float anchorX, float bottom, float scale, float left, float right)
    {
        Close();

        if (items.Length == 0) return;

        _items = items;
        _scale = scale;

        float width = 0f;
        foreach (string item in items) width = MathF.Max(width, Labels.Measure(item, scale).X);

        width += PaddingX * 2f * scale;
        float height = items.Length * RowHeight * scale + PaddingY * 2f * scale;
        _size = new Vector2(MathF.Ceiling(width), MathF.Ceiling(height));

        // Centrado en el icono, pero sin asomar por fuera de la barra: ahí el hit-test
        // de la ventana devuelve HTTRANSPARENT y el clic se iría a lo que haya debajo.
        float x = Math.Clamp(anchorX - _size.X * 0.5f, left, MathF.Max(left, right - _size.X));
        _origin = new Vector2(x, bottom - _size.Y);

        ContainerVisual menu = _compositor.CreateContainerVisual();
        menu.Size = _size;
        menu.Offset = new Vector3(_origin.X, _origin.Y, 0f);

        CompositionRoundedRectangleGeometry round = _compositor.CreateRoundedRectangleGeometry();
        round.Size = _size;
        round.CornerRadius = new Vector2(10f * scale);
        menu.Clip = _compositor.CreateGeometricClip(round);

        SpriteVisual chip = _compositor.CreateSpriteVisual();
        chip.RelativeSizeAdjustment = Vector2.One;
        chip.Brush = _compositor.CreateColorBrush(Color.FromArgb(242, 32, 32, 38));
        menu.Children.InsertAtBottom(chip);

        SpriteVisual hot = _compositor.CreateSpriteVisual();
        hot.Size = new Vector2(_size.X - PaddingY * 2f * scale, RowHeight * scale);
        hot.Brush = _compositor.CreateColorBrush(Color.FromArgb(46, 255, 255, 255));
        hot.Opacity = 0f;

        CompositionRoundedRectangleGeometry hotRound = _compositor.CreateRoundedRectangleGeometry();
        hotRound.Size = hot.Size;
        hotRound.CornerRadius = new Vector2(6f * scale);
        hot.Clip = _compositor.CreateGeometricClip(hotRound);
        menu.Children.InsertAtTop(hot);

        SpriteVisual text = _compositor.CreateSpriteVisual();
        text.Size = _size;
        text.Brush = CreateTextBrush(items, scale);
        menu.Children.InsertAtTop(text);

        _parent.Children.InsertAtTop(menu);
        _menu = menu;
        _hot = hot;
        _hotIndex = -1;
    }

    public void Close()
    {
        if (_menu is null) return;

        _parent.Children.Remove(_menu);
        _menu.Dispose();
        _menu = null;
        _hot = null;
        _hotIndex = -1;
    }

    /// <summary>
    /// Todas las filas de texto en una sola superficie transparente, encima del resalte.
    /// Una superficie por fila costaría lo mismo y daría más piezas que colocar.
    /// </summary>
    private CompositionSurfaceBrush CreateTextBrush(string[] items, float scale)
    {
        CompositionDrawingSurface surface = _graphics.CreateDrawingSurface(
            new global::Windows.Foundation.Size(_size.X, _size.Y),
            DirectXPixelFormat.B8G8R8A8UIntNormalized,
            DirectXAlphaMode.Premultiplied);

        ICompositionDrawingSurfaceInterop interop = surface.As<ICompositionDrawingSurfaceInterop>();
        Guid iid = typeof(ID2D1DeviceContext).GUID;

        System.Drawing.Point offset;
        interop.BeginDraw(null, &iid, out object contextObject, &offset);
        try
        {
            var context = (ID2D1DeviceContext)contextObject;
            context.SetDpi(96, 96);

            D2D1_COLOR_F transparent = default;
            context.Clear(&transparent);

            for (int i = 0; i < items.Length; i++)
            {
                float top = offset.Y + PaddingY * scale + i * RowHeight * scale;
                Labels.DrawRow(
                    context,
                    items[i],
                    scale,
                    new System.Drawing.Point((int)(offset.X + PaddingX * scale), (int)top),
                    RowHeight * scale);
            }
        }
        finally
        {
            interop.EndDraw();
        }

        return _compositor.CreateSurfaceBrush(surface);
    }
}
