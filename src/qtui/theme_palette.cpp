#include "theme_palette.h"
#include "gfx/theme.h"

ThemePalette::ThemePalette(QObject* parent)
    : QObject(parent)
{
}

QColor ThemePalette::bg() const {
    const auto& c = gfx::active_theme().bg;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::surface() const {
    const auto& c = gfx::active_theme().surface;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::surfaceHi() const {
    const auto& c = gfx::active_theme().surface_hi;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::border() const {
    const auto& c = gfx::active_theme().border;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::accent() const {
    const auto& c = gfx::active_theme().accent;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::accentDim() const {
    const auto& c = gfx::active_theme().accent_dim;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::text() const {
    const auto& c = gfx::active_theme().text;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::textDim() const {
    const auto& c = gfx::active_theme().text_dim;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::textFaint() const {
    const auto& c = gfx::active_theme().text_faint;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::folder() const {
    const auto& c = gfx::active_theme().folder;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::favorite() const {
    const auto& c = gfx::active_theme().favorite;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::danger() const {
    const auto& c = gfx::active_theme().danger;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::warn() const {
    const auto& c = gfx::active_theme().warn;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::ok() const {
    const auto& c = gfx::active_theme().ok;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::imgBg() const {
    const auto& c = gfx::active_theme().img_bg;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

QColor ThemePalette::stripBg() const {
    const auto& c = gfx::active_theme().strip_bg;
    return QColor::fromRgb(c.r, c.g, c.b, c.a);
}

void ThemePalette::setThemeIndex(int index) {
    if (index < 0 || index >= gfx::THEME_COUNT) {
        return;  // out of range, silently ignore
    }
    gfx::set_theme(static_cast<gfx::ThemeId>(index));
    emit changed();
}
