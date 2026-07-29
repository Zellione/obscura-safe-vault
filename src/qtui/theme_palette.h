#pragma once

#include <QObject>
#include <QColor>

// Wraps gfx::Theme to expose it as a context property in QML.
// Provides QColor properties for each theme token and a setThemeIndex() invokable
// to cycle through the four built-in presets (RefinedSlate, Light, HighContrast, Midnight).
class ThemePalette : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor bg READ bg NOTIFY changed)
    Q_PROPERTY(QColor surface READ surface NOTIFY changed)
    Q_PROPERTY(QColor surfaceHi READ surfaceHi NOTIFY changed)
    Q_PROPERTY(QColor border READ border NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor accentDim READ accentDim NOTIFY changed)
    Q_PROPERTY(QColor text READ text NOTIFY changed)
    Q_PROPERTY(QColor textDim READ textDim NOTIFY changed)
    Q_PROPERTY(QColor textFaint READ textFaint NOTIFY changed)
    Q_PROPERTY(QColor folder READ folder NOTIFY changed)
    Q_PROPERTY(QColor favorite READ favorite NOTIFY changed)
    Q_PROPERTY(QColor danger READ danger NOTIFY changed)
    Q_PROPERTY(QColor warn READ warn NOTIFY changed)
    Q_PROPERTY(QColor ok READ ok NOTIFY changed)
    Q_PROPERTY(QColor imgBg READ imgBg NOTIFY changed)
    Q_PROPERTY(QColor stripBg READ stripBg NOTIFY changed)

public:
    explicit ThemePalette(QObject* parent = nullptr);

    // Color property accessors
    QColor bg() const;
    QColor surface() const;
    QColor surfaceHi() const;
    QColor border() const;
    QColor accent() const;
    QColor accentDim() const;
    QColor text() const;
    QColor textDim() const;
    QColor textFaint() const;
    QColor folder() const;
    QColor favorite() const;
    QColor danger() const;
    QColor warn() const;
    QColor ok() const;
    QColor imgBg() const;
    QColor stripBg() const;

    // Invokable to cycle through theme presets
    Q_INVOKABLE void setThemeIndex(int index);

signals:
    void changed();
};
