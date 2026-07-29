// Headless (QT_QPA_PLATFORM=offscreen) behavior test for SecureTextField:
// typing, backspace, select-all + type-over, Ctrl+C refusal, clearSecret wipe.
#include <QGuiApplication>
#include <QKeyEvent>
#include <cassert>

#include "secure_text_field.h"

static void key(SecureTextField& f, int k, Qt::KeyboardModifiers m = {}, const QString& text = {})
{
    QKeyEvent ev(QEvent::KeyPress, k, m, text);
    f.testOnlyKeyPress(&ev);
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    SecureTextField f;

    key(f, Qt::Key_A, {}, "a");
    key(f, Qt::Key_B, {}, "b");
    key(f, Qt::Key_Odiaeresis, {}, QString::fromUtf8("\xC3\xB6"));  // multi-byte UTF-8
    assert(f.length() == 3);

    key(f, Qt::Key_Backspace);
    assert(f.length() == 2);                        // whole character removed, not one byte

    key(f, Qt::Key_A, Qt::ControlModifier);         // select all
    key(f, Qt::Key_C, Qt::ControlModifier);         // must be a no-op for a secure field
    assert(f.model().selection_text().empty());

    key(f, Qt::Key_X, {}, "x");                     // type-over replaces selection
    assert(f.length() == 1);

    f.clearSecret();
    assert(f.length() == 0 && f.model().bytes().empty());

    puts("secure_field OK");
    return 0;
}
