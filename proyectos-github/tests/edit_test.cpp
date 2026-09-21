// El modelo del campo de texto. Es el archivo de pruebas más largo de la fase y con
// razón: el cursor y la selección se equivocan en silencio. Un rango mal contado no da
// error, solo borra una letra de más, y eso solo se nota escribiendo justo esa palabra.
//
// El criterio de aceptación de la fase —escribir «Revisión año niño» sin problemas— se
// cumple aquí antes de que exista un píxel. Las tildes y la eñe son un solo wchar_t cada
// una en la forma compuesta, que es la que produce un teclado español; las combinantes,
// que son dos, también tienen su caso.

#include <doctest/doctest.h>

#include <string>

#include "ui/Edit.h"

namespace {

// La frase del criterio. Diecisiete unidades: ocho de «Revisión», espacio, tres de
// «año», espacio y cuatro de «niño». La eñe y la o con tilde van compuestas.
constexpr wchar_t kFrase[] = L"Revisión año niño";

// Teclear de verdad: un WM_CHAR por letra, que es como llega.
void Teclear(Ui::Editor& editor, std::wstring_view text) {
    for (const wchar_t unit : text) {
        editor.Insert(std::wstring_view{&unit, 1});
    }
}

}  // namespace

TEST_CASE("escribir «Revisión año niño» letra a letra deja exactamente eso") {
    Ui::Editor editor;
    Teclear(editor, kFrase);

    CHECK(editor.Text() == std::wstring{kFrase});
    // Diecisiete y no veinte: la tilde y la eñe compuestas ocupan una unidad cada una.
    // Si esto sale 19, el texto llegó descompuesto y el cursor irá mal toda la vida.
    CHECK(editor.Text().size() == 17);
    CHECK(editor.Cursor().head == 17);
    CHECK(editor.Cursor().Empty());
}

TEST_CASE("retroceder sobre una letra con tilde se lleva la letra entera") {
    Ui::Editor editor;
    Teclear(editor, L"Revisión");
    CHECK(editor.Text().size() == 8);

    editor.Backspace();  // la n
    editor.Backspace();  // la ó, que es una sola unidad compuesta
    CHECK(editor.Text() == std::wstring{L"Revisi"});
}

TEST_CASE("retroceder sobre una tilde combinante se lleva la letra y la tilde") {
    Ui::Editor editor;
    // La misma «ó» pero en dos unidades: o + acento combinante. Es lo que llega si el
    // texto viene de un archivo en forma descompuesta. Sin tratarla, un retroceso deja
    // una «o» con un acento suelto detrás y la siguiente pulsación borra la «o» dejando
    // el acento huérfano, que se dibuja como un cuadro.
    editor.SetText(L"Revisión");
    CHECK(editor.Text().size() == 9);

    editor.Backspace();  // la n
    editor.Backspace();  // la o y su acento, de una vez
    CHECK(editor.Text() == std::wstring{L"Revisi"});
}

TEST_CASE("moverse de una en una nunca parte un par suplente") {
    Ui::Editor editor;
    // Un emoji son dos unidades UTF-16. Un cursor entre las dos mitades mide mal y borra
    // peor: DirectWrite dibujaría un cuadro donde había una cara.
    editor.SetText(L"a\U0001F600b");
    CHECK(editor.Text().size() == 4);

    editor.MoveCaret(Ui::Move::Start, false);
    editor.MoveCaret(Ui::Move::Right, false);
    CHECK(editor.Cursor().head == 1);
    editor.MoveCaret(Ui::Move::Right, false);
    CHECK(editor.Cursor().head == 3);  // se salta las dos mitades de golpe, no 2
    editor.MoveCaret(Ui::Move::Right, false);
    CHECK(editor.Cursor().head == 4);

    editor.MoveCaret(Ui::Move::Left, false);
    CHECK(editor.Cursor().head == 3);
    editor.MoveCaret(Ui::Move::Left, false);
    CHECK(editor.Cursor().head == 1);
}

TEST_CASE("un clic en medio de un par suplente cae en un sitio legal") {
    Ui::Editor editor;
    editor.SetText(L"a\U0001F600b");
    // DirectWrite puede devolver el índice 2 si se pincha en la mitad derecha del glifo.
    editor.PlaceCaret(2, false);
    CHECK(editor.Cursor().head == 1);
}

TEST_CASE("Ctrl+izquierda va saltando de palabra en palabra") {
    Ui::Editor editor;
    editor.SetText(kFrase);
    CHECK(editor.Cursor().head == 17);

    editor.MoveCaret(Ui::Move::WordLeft, false);
    CHECK(editor.Cursor().head == 13);  // el principio de «niño»
    editor.MoveCaret(Ui::Move::WordLeft, false);
    CHECK(editor.Cursor().head == 9);   // el principio de «año»
    editor.MoveCaret(Ui::Move::WordLeft, false);
    CHECK(editor.Cursor().head == 0);   // el principio de «Revisión»
    editor.MoveCaret(Ui::Move::WordLeft, false);
    CHECK(editor.Cursor().head == 0);   // y ahí se queda
}

TEST_CASE("la eñe y las vocales con tilde son letras, no puntuación") {
    Ui::Editor editor;
    editor.SetText(kFrase);
    // Si la eñe contara como puntuación, «niño» serían tres palabras y Ctrl+flecha
    // pararía en medio. Es el fallo clásico de clasificar con isalpha de ASCII.
    const Ui::Range palabra = Ui::WordAt(editor.Text(), 14);
    CHECK(palabra.begin == 13);
    CHECK(palabra.end == 17);

    const Ui::Range conTilde = Ui::WordAt(editor.Text(), 2);
    CHECK(conTilde.begin == 0);
    CHECK(conTilde.end == 8);
}

TEST_CASE("doble clic selecciona la palabra y el triple la línea") {
    Ui::Editor editor;
    editor.SetText(kFrase);

    editor.SelectWordAt(10);  // dentro de «año»
    CHECK(editor.Selected() == std::wstring{L"año"});

    editor.SelectAll();
    CHECK(editor.Selected() == std::wstring{kFrase});
}

TEST_CASE("Shift extiende desde el ancla y la flecha sola colapsa") {
    Ui::Editor editor;
    editor.SetText(kFrase);
    editor.MoveCaret(Ui::Move::Start, false);

    editor.MoveCaret(Ui::Move::Right, true);
    editor.MoveCaret(Ui::Move::Right, true);
    editor.MoveCaret(Ui::Move::Right, true);
    CHECK(editor.Selected() == std::wstring{L"Rev"});
    CHECK(editor.Cursor().anchor == 0);

    // Y encoge por donde creció: con un rango ordenado en vez de ancla y extremo, esto
    // agrandaría la selección en lugar de reducirla.
    editor.MoveCaret(Ui::Move::Left, true);
    CHECK(editor.Selected() == std::wstring{L"Re"});

    // La flecha sin Shift no mueve: colapsa al extremo.
    editor.MoveCaret(Ui::Move::Right, false);
    CHECK(editor.Cursor().Empty());
    CHECK(editor.Cursor().head == 2);
}

TEST_CASE("escribir con algo seleccionado lo reemplaza") {
    Ui::Editor editor;
    editor.SetText(kFrase);
    editor.SelectWordAt(14);  // «niño»
    editor.Insert(L"x");
    CHECK(editor.Text() == std::wstring{L"Revisión año x"});
    CHECK(editor.Cursor().Empty());
    CHECK(editor.Cursor().head == 14);
}

TEST_CASE("Inicio y Fin van a los extremos") {
    Ui::Editor editor;
    editor.SetText(kFrase);
    editor.MoveCaret(Ui::Move::Start, false);
    CHECK(editor.Cursor().head == 0);
    editor.MoveCaret(Ui::Move::End, false);
    CHECK(editor.Cursor().head == 17);
    // Con Shift, desde donde estaba.
    editor.MoveCaret(Ui::Move::Start, true);
    CHECK(editor.Selected() == std::wstring{kFrase});
}

TEST_CASE("un deshacer devuelve la última palabra, ni la frase ni una letra") {
    Ui::Editor editor;
    Teclear(editor, kFrase);

    // El espacio cierra el grupo, así que lo tecleado se parte en «Revisión », «año » y
    // «niño». Sin agrupar, harían falta cuatro deshaceres para quitar «niño»; agrupando
    // de más, el primero dejaría el campo vacío.
    CHECK(editor.Undo());
    CHECK(editor.Text() == std::wstring{L"Revisión año "});
    CHECK(editor.Undo());
    CHECK(editor.Text() == std::wstring{L"Revisión "});
    CHECK(editor.Undo());
    CHECK(editor.Text().empty());
    CHECK_FALSE(editor.Undo());
}

TEST_CASE("rehacer devuelve lo deshecho y el cursor donde estaba") {
    Ui::Editor editor;
    Teclear(editor, kFrase);
    editor.Undo();
    CHECK(editor.Redo());
    CHECK(editor.Text() == std::wstring{kFrase});
    CHECK(editor.Cursor().head == 17);
    CHECK_FALSE(editor.Redo());
}

TEST_CASE("escribir después de deshacer borra el rehacer") {
    Ui::Editor editor;
    Teclear(editor, L"hola");
    editor.Undo();
    CHECK(editor.CanRedo());
    editor.Insert(L"x");
    // Si el rehacer sobreviviera, rehacer aquí mezclaría dos historias distintas.
    CHECK_FALSE(editor.CanRedo());
}

TEST_CASE("pegar es siempre un paso suyo") {
    Ui::Editor editor;
    Teclear(editor, L"hola");
    editor.Insert(L" mundo entero");  // un pegado, no tecleo

    CHECK(editor.Undo());
    // Se va el pegado entero de una vez, y lo tecleado sigue ahí.
    CHECK(editor.Text() == std::wstring{L"hola"});
}

TEST_CASE("mover el cursor corta el grupo de deshacer") {
    Ui::Editor editor;
    Teclear(editor, L"hola");
    editor.MoveCaret(Ui::Move::Start, false);
    editor.MoveCaret(Ui::Move::End, false);
    Teclear(editor, L"dos");

    // «dos» y «hola» son dos pasos: entre medias el cursor se movió, y eso termina lo
    // que se acababa de escribir aunque no haya habido ningún espacio.
    CHECK(editor.Undo());
    CHECK(editor.Text() == std::wstring{L"hola"});
}

TEST_CASE("retroceder seguido se deshace de una vez") {
    Ui::Editor editor;
    editor.SetText(L"Revisión");
    editor.Backspace();
    editor.Backspace();
    editor.Backspace();
    CHECK(editor.Text() == std::wstring{L"Revis"});

    // Tres retrocesos son un arrepentimiento, no tres.
    CHECK(editor.Undo());
    CHECK(editor.Text() == std::wstring{L"Revisión"});
}

TEST_CASE("suprimir seguido también se agrupa, y hacia el otro lado") {
    Ui::Editor editor;
    editor.SetText(L"Revisión");
    editor.MoveCaret(Ui::Move::Start, false);
    editor.DeleteForward();
    editor.DeleteForward();
    CHECK(editor.Text() == std::wstring{L"visión"});
    CHECK(editor.Undo());
    CHECK(editor.Text() == std::wstring{L"Revisión"});
}

TEST_CASE("el límite de longitud no parte un par suplente") {
    Ui::Editor editor;
    editor.SetMaxLength(3);
    // Caben «a» y el emoji, que son tres unidades justas.
    editor.SetText(L"a\U0001F600");
    CHECK(editor.Text().size() == 3);

    // Y una más no entra: truncar a 3 en medio del segundo emoji dejaría media cara.
    Ui::Editor otro;
    otro.SetMaxLength(2);
    otro.SetText(L"a\U0001F600");
    CHECK(otro.Text() == std::wstring{L"a"});
}

TEST_CASE("con el campo lleno, teclear no hace nada") {
    Ui::Editor editor;
    editor.SetMaxLength(5);
    editor.SetText(L"12345");
    CHECK_FALSE(editor.Insert(L"6"));
    CHECK(editor.Text() == std::wstring{L"12345"});
}

TEST_CASE("retroceder al principio y suprimir al final no hacen nada") {
    Ui::Editor editor;
    editor.SetText(L"ab");
    editor.MoveCaret(Ui::Move::Start, false);
    CHECK_FALSE(editor.Backspace());
    editor.MoveCaret(Ui::Move::End, false);
    CHECK_FALSE(editor.DeleteForward());
    CHECK(editor.Text() == std::wstring{L"ab"});
}

TEST_CASE("un campo vacío aguanta todo lo que se le pida") {
    Ui::Editor editor;
    CHECK_FALSE(editor.Backspace());
    CHECK_FALSE(editor.DeleteForward());
    CHECK_FALSE(editor.Undo());
    CHECK_FALSE(editor.Redo());
    CHECK_FALSE(editor.MoveCaret(Ui::Move::WordLeft, false));
    CHECK(editor.SelectAll() == false);  // no hay nada que seleccionar
    CHECK(editor.Text().empty());
    CHECK(Ui::WordAt(L"", 0) == Ui::Range{0, 0});
}

TEST_CASE("la composición del IME no está en el texto hasta que se confirma") {
    Ui::Editor editor;
    editor.SetText(L"hola ");

    editor.BeginComposition();
    editor.UpdateComposition(L"にほん", 3);

    // Lo que se mide y se dibuja lleva la composición; lo que lee el resto del programa,
    // no. Así nadie tiene que saber que existe un IME para leer el campo.
    CHECK(editor.Text() == std::wstring{L"hola "});
    CHECK(editor.Display() == std::wstring{L"hola にほん"});
    CHECK(editor.Composing());
    CHECK(editor.Composition() == Ui::Range{5, 8});
    CHECK(editor.DisplayCaret() == 8);

    editor.CommitComposition(L"日本");
    CHECK_FALSE(editor.Composing());
    CHECK(editor.Text() == std::wstring{L"hola 日本"});
    CHECK(editor.Display() == editor.Text());
}

TEST_CASE("cancelar una composición no deja rastro ni en el texto ni en el historial") {
    Ui::Editor editor;
    Teclear(editor, L"hola");
    editor.BeginComposition();
    editor.UpdateComposition(L"にほん", 3);
    editor.CancelComposition();

    CHECK(editor.Text() == std::wstring{L"hola"});
    CHECK(editor.Display() == std::wstring{L"hola"});
    // Un deshacer se lleva «hola» y no una sílaba a medias: la composición nunca llegó
    // a entrar en el historial.
    CHECK(editor.Undo());
    CHECK(editor.Text().empty());
}

TEST_CASE("una palabra compuesta se deshace de una pieza") {
    Ui::Editor editor;
    editor.SetText(L"hola ");
    editor.BeginComposition();
    editor.UpdateComposition(L"にほん", 3);
    editor.CommitComposition(L"日本");

    CHECK(editor.Undo());
    CHECK(editor.Text() == std::wstring{L"hola "});
}

TEST_CASE("cambiar el texto por programa vacía el historial") {
    Ui::Editor editor;
    Teclear(editor, L"hola");
    CHECK(editor.CanUndo());
    editor.SetText(L"otro");
    // Lo que había deja de tener sentido cuando el texto lo pone otro: deshacer aquí
    // devolvería un texto que el usuario nunca escribió.
    CHECK_FALSE(editor.CanUndo());
    CHECK(editor.Cursor().head == 4);
}
