#include "secure_text_field.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QPainter>

#include "ui/text_input_model.h"   // utf8_char_count

SecureTextField::SecureTextField(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setFlag(ItemAcceptsInputMethod, true);
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
}

int SecureTextField::length() const
{
    return static_cast<int>(ui::utf8_char_count(model_.bytes()));
}

void SecureTextField::clearSecret()
{
    model_.clear();
    emit lengthChanged();
    update();
}

void SecureTextField::insertWiped(const QString& text)
{
    QByteArray utf8 = text.toUtf8();
    model_.insert(std::string_view(utf8.constData(), static_cast<size_t>(utf8.size())));
    utf8.fill('\0');   // best-effort wipe of the transient copy (detached, so no COW alias)
}

void SecureTextField::keyPressEvent(QKeyEvent* e)
{
    const bool ctrl  = e->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);

    switch (e->key()) {
    case Qt::Key_Backspace: model_.backspace(); break;
    case Qt::Key_Delete:    model_.del(); break;
    case Qt::Key_Left:      model_.move_left(ctrl, shift); break;
    case Qt::Key_Right:     model_.move_right(ctrl, shift); break;
    case Qt::Key_Home:      model_.move_home(shift); break;
    case Qt::Key_End:       model_.move_end(shift); break;
    case Qt::Key_Return:
    case Qt::Key_Enter:     emit accepted(); e->accept(); return;
    default:
        if (ctrl && e->key() == Qt::Key_A) { model_.select_all(); break; }
        if (ctrl && (e->key() == Qt::Key_C || e->key() == Qt::Key_X)) { e->accept(); return; }  // refused
        if (ctrl && e->key() == Qt::Key_V) {
            insertWiped(QGuiApplication::clipboard()->text());
            break;
        }
        if (!e->text().isEmpty() && e->text().at(0).isPrint()) { insertWiped(e->text()); break; }
        QQuickPaintedItem::keyPressEvent(e);
        return;
    }
    e->accept();
    emit lengthChanged();
    update();
}

void SecureTextField::inputMethodEvent(QInputMethodEvent* e)
{
    // Commit string only; the preedit is never stored or painted.
    if (!e->commitString().isEmpty()) {
        insertWiped(e->commitString());
        emit lengthChanged();
        update();
    }
    e->accept();
}

QVariant SecureTextField::inputMethodQuery(Qt::InputMethodQuery q) const
{
    switch (q) {
    case Qt::ImEnabled: return true;
    case Qt::ImHints:
        return int(Qt::ImhHiddenText | Qt::ImhSensitiveData
                   | Qt::ImhNoPredictiveText | Qt::ImhNoAutoUppercase);
    case Qt::ImSurroundingText: return QString();   // never reveal content
    default: return QQuickPaintedItem::inputMethodQuery(q);
    }
}

void SecureTextField::paint(QPainter* p)
{
    // Mask dots + caret. Count-derived geometry only — no plaintext is painted,
    // so QQuickPaintedItem's internal raster target never holds secret bytes.
    const int   n = length();
    const qreal r = height() * 0.18;
    const qreal step = r * 3.2;
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor("#c8ccd4"));
    for (int i = 0; i < n; ++i)
        p->drawEllipse(QPointF(8 + r + i * step, height() / 2.0), r, r);
    if (hasActiveFocus()) {
        p->setBrush(QColor("#5a9cf8"));
        p->drawRect(QRectF(8 + n * step, height() * 0.2, 2, height() * 0.6));
    }
}
