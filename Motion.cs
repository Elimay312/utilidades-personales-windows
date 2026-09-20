using System.Numerics;
using Windows.UI.Composition;

namespace QuickLook;

/// <summary>
/// Todas las animaciones del panel, en un sitio.
///
/// <para>
/// <b>Nada de esto corre en nuestro hilo.</b> Son animaciones de Composition, asi que las
/// ejecuta el proceso de DWM: siguen yendo finas aunque el hilo de UI se quede decodificando
/// un PDF de 40 MB. Es el mismo argumento que decidio el stack del dock, y aqui pesa mas
/// todavia porque lo que se anima es lo primero que se ve.
/// </para>
///
/// <para>
/// <b>Muelle para la forma, curva para la opacidad.</b> El tamano entra con
/// <c>SpringVector3NaturalMotionAnimation</c> —un rebote minimo, el que hace que se sienta
/// fisico en vez de dibujado— y la opacidad con una cubica que termina antes, para que el
/// panel ya se lea mientras el muelle todavia esta asentandose. Las dos a la vez, sin
/// esperar una a otra.
/// </para>
/// </summary>
internal static class Motion
{
    /// <summary>De que tamano nace el panel. Lo bastante lejos de 1 para que se vea crecer.</summary>
    public const float OpenScale = 0.86f;

    /// <summary>A que tamano se va al cerrarse. Mas cerca de 1: salir es mas rapido que entrar.</summary>
    private const float CloseScale = 0.92f;

    /// <summary>
    /// 0.82 deja UN rebote corto. Por debajo de 0.7 el panel oscila y parece de goma; a 1
    /// no rebota nada y se pierde justo la sensacion que se buscaba.
    /// </summary>
    private const float Damping = 0.82f;

    /// <summary>
    /// El muelle del morph va mas amortiguado que el de la apertura (0.9 frente a 0.82) a
    /// proposito: lo que rebota aqui es el TAMANO de la tarjeta, y un sobrepaso se sale
    /// del recorte de las esquinas y se ve como un mordisco en el borde. Abriendo rebota
    /// la escala entera, que no se recorta contra nada.
    /// </summary>
    private const float MorphDamping = 0.9f;

    private static readonly TimeSpan Period = TimeSpan.FromMilliseconds(40);
    private static readonly TimeSpan MorphPeriod = TimeSpan.FromMilliseconds(45);
    private static readonly TimeSpan FadeIn = TimeSpan.FromMilliseconds(260);
    private static readonly TimeSpan FadeOut = TimeSpan.FromMilliseconds(180);

    /// <summary>
    /// El cruce entre el contenido viejo y el nuevo. Corto y solapado: si el viejo sale del
    /// todo antes de que entre el nuevo hay un parpadeo a tarjeta vacia en medio, y eso
    /// rompe la sensacion de que es la MISMA tarjeta cambiando.
    /// </summary>
    private static readonly TimeSpan Cross = TimeSpan.FromMilliseconds(120);

    /// <summary>
    /// El panel crece hasta su sitio. Se da por hecho que <paramref name="visual"/> ya
    /// esta en el estado de partida —escala <see cref="OpenScale"/> y opacidad 0— y que su
    /// <c>CenterPoint</c> apunta a donde tiene que nacer.
    /// </summary>
    public static void Open(Compositor compositor, Visual visual)
    {
        SpringVector3NaturalMotionAnimation grow = compositor.CreateSpringVector3Animation();
        grow.DampingRatio = Damping;
        grow.Period = Period;
        grow.FinalValue = Vector3.One;
        visual.StartAnimation("Scale", grow);

        // La opacidad entra con una cubica que arranca rapido y frena al final (0.16, 1,
        // 0.3, 1): a mitad del muelle el panel ya se lee.
        ScalarKeyFrameAnimation fade = compositor.CreateScalarKeyFrameAnimation();
        fade.Duration = FadeIn;
        fade.InsertKeyFrame(1f, 1f, compositor.CreateCubicBezierEasingFunction(
            new Vector2(0.16f, 1f), new Vector2(0.3f, 1f)));
        visual.StartAnimation("Opacity", fade);
    }

    /// <summary>
    /// El panel se encoge y se va, y <paramref name="done"/> se llama cuando la animacion
    /// ha TERMINADO de verdad.
    ///
    /// <para>
    /// El aviso sale de un <c>CompositionScopedBatch</c> y no de un temporizador nuestro: un
    /// temporizador acierta el tiempo pero no el fotograma, y destruir la ventana un
    /// fotograma antes de tiempo es exactamente el corte seco que se queria evitar.
    /// </para>
    /// </summary>
    public static void Close(Compositor compositor, Visual visual, Action done)
    {
        CompositionScopedBatch batch = compositor.CreateScopedBatch(CompositionBatchTypes.Animation);

        Vector3KeyFrameAnimation shrink = compositor.CreateVector3KeyFrameAnimation();
        shrink.Duration = FadeOut;
        shrink.InsertKeyFrame(1f, new Vector3(CloseScale, CloseScale, 1f),
            compositor.CreateCubicBezierEasingFunction(new Vector2(0.4f, 0f), new Vector2(1f, 1f)));
        visual.StartAnimation("Scale", shrink);

        ScalarKeyFrameAnimation fade = compositor.CreateScalarKeyFrameAnimation();
        fade.Duration = FadeOut;
        fade.InsertKeyFrame(1f, 0f);
        visual.StartAnimation("Opacity", fade);

        batch.End();
        batch.Completed += (_, _) => done();
    }

    /// <summary>
    /// La tarjeta cambia de forma sin cerrarse, y lo de dentro se cruza.
    ///
    /// <para>
    /// Se animan cuatro cosas a la vez y todas tienen que llegar juntas, o se nota: el
    /// tamano de la tarjeta, el de su geometria de esquinas —si no la acompana, el material
    /// queda recortado a la forma vieja—, el radio de esas esquinas, y la posicion, porque
    /// la tarjeta esta centrada y al cambiar de tamano su esquina se mueve.
    /// </para>
    /// </summary>
    public static void Morph(
        Compositor compositor,
        ContainerVisual card,
        CompositionRoundedRectangleGeometry round,
        Vector2 size,
        Vector3 offset,
        Vector2 radius,
        Visual outgoing,
        Visual incoming,
        Action done)
    {
        CompositionScopedBatch batch = compositor.CreateScopedBatch(CompositionBatchTypes.Animation);

        SpringVector2NaturalMotionAnimation resize = compositor.CreateSpringVector2Animation();
        resize.DampingRatio = MorphDamping;
        resize.Period = MorphPeriod;
        resize.FinalValue = size;
        card.StartAnimation("Size", resize);
        round.StartAnimation("Size", resize);

        SpringVector3NaturalMotionAnimation move = compositor.CreateSpringVector3Animation();
        move.DampingRatio = MorphDamping;
        move.Period = MorphPeriod;
        move.FinalValue = offset;
        card.StartAnimation("Offset", move);

        Vector2KeyFrameAnimation corners = compositor.CreateVector2KeyFrameAnimation();
        corners.Duration = FadeIn;
        corners.InsertKeyFrame(1f, radius);
        round.StartAnimation("CornerRadius", corners);

        CrossFade(compositor, outgoing, incoming);

        batch.End();
        batch.Completed += (_, _) => done();
    }

    /// <summary>
    /// El viejo sale creciendo un poco y el nuevo entra encogido: los dos se mueven en el
    /// mismo sentido, asi que se lee como que uno pasa por delante del otro y no como dos
    /// imagenes fundidas.
    /// </summary>
    private static void CrossFade(Compositor compositor, Visual outgoing, Visual incoming)
    {
        ScalarKeyFrameAnimation out_ = compositor.CreateScalarKeyFrameAnimation();
        out_.Duration = Cross;
        out_.InsertKeyFrame(1f, 0f);
        outgoing.StartAnimation("Opacity", out_);

        Vector3KeyFrameAnimation grow = compositor.CreateVector3KeyFrameAnimation();
        grow.Duration = Cross;
        grow.InsertKeyFrame(1f, new Vector3(1.04f, 1.04f, 1f));
        outgoing.StartAnimation("Scale", grow);

        ScalarKeyFrameAnimation in_ = compositor.CreateScalarKeyFrameAnimation();
        in_.Duration = Cross;
        in_.InsertKeyFrame(1f, 1f);
        incoming.StartAnimation("Opacity", in_);

        Vector3KeyFrameAnimation settle = compositor.CreateVector3KeyFrameAnimation();
        settle.Duration = Cross;
        settle.InsertKeyFrame(1f, Vector3.One);
        incoming.StartAnimation("Scale", settle);
    }
}
