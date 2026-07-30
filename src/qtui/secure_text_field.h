#pragma once

// SecureTextField — the ONLY password entry widget in osv-qt (spec §4).
// Key/IME events feed the existing mlock'd ui::SecureTextInput; painting shows
// mask dots derived from a character COUNT only. Qt's TextInput is never used
// for secrets. Copy/cut are refused (the model's secure() contract); paste in
// is allowed, with best-effort wipe of the transient Qt-side copies.

#include <QQuickPaintedItem>

#include "ui/secure_text_input.h"

class SecureTextField : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(int length READ length NOTIFY lengthChanged)
public:
    explicit SecureTextField(QQuickItem* parent = nullptr);

    [[nodiscard]] int length() const;
    [[nodiscard]] ui::SecureTextInput& model() noexcept { return model_; }

    Q_INVOKABLE void clearSecret();

    void paint(QPainter* p) override;

    // Test seam: keyPressEvent is protected; headless tests drive it directly.
    void testOnlyKeyPress(QKeyEvent* e) { keyPressEvent(e); }

signals:
    void lengthChanged();
    void accepted();   // Enter/Return

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void inputMethodEvent(QInputMethodEvent* e) override;
    [[nodiscard]] QVariant inputMethodQuery(Qt::InputMethodQuery q) const override;

private:
    void insertWiped(const QString& text);   // UTF-8 convert → model insert → wipe copies
    ui::SecureTextInput model_;
};
