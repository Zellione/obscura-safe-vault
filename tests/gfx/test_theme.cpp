#include "test_framework.h"

#include <array>
#include <string_view>

#include "gfx/theme.h"

namespace {

// The 16 colour tokens of a Theme, in one list, so a test can sweep them all.
std::array<gfx::Color, 16> tokens(const gfx::Theme& t)
{
    return {t.bg, t.surface, t.surface_hi, t.border, t.accent, t.accent_dim,
            t.text, t.text_dim, t.text_faint, t.folder, t.favorite, t.danger,
            t.warn, t.ok, t.img_bg, t.strip_bg};
}

bool color_eq(const gfx::Color& a, const gfx::Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Restore the default theme so test ordering can't leak a selection.
struct ThemeGuard {
    gfx::ThemeId saved = gfx::active_theme_id();
    ~ThemeGuard() { gfx::set_theme(saved); }
};

} // namespace

TEST(theme_presets_define_all_tokens_opaque)
{
    // Every preset fills every token with an opaque colour — no token left at a
    // half-transparent / zeroed sentinel.
    for (int i = 0; i < gfx::THEME_COUNT; ++i) {
        const auto& t = gfx::theme_preset(static_cast<gfx::ThemeId>(i));
        for (const auto& c : tokens(t)) CHECK_EQ(static_cast<int>(c.a), 255);
    }
}

TEST(theme_presets_are_distinct)
{
    // The presets are genuinely different palettes (backgrounds differ pairwise).
    for (int i = 0; i < gfx::THEME_COUNT; ++i)
        for (int j = i + 1; j < gfx::THEME_COUNT; ++j) {
            const auto& a = gfx::theme_preset(static_cast<gfx::ThemeId>(i));
            const auto& b = gfx::theme_preset(static_cast<gfx::ThemeId>(j));
            CHECK_FALSE(color_eq(a.bg, b.bg));
        }
}

TEST(theme_names_and_slugs_present)
{
    for (int i = 0; i < gfx::THEME_COUNT; ++i) {
        const auto id = static_cast<gfx::ThemeId>(i);
        CHECK_TRUE(std::string_view(gfx::theme_name(id)).size() > 0);
        CHECK_TRUE(std::string_view(gfx::theme_slug(id)).size() > 0);
    }
}

TEST(theme_slug_round_trips)
{
    for (int i = 0; i < gfx::THEME_COUNT; ++i) {
        const auto id = static_cast<gfx::ThemeId>(i);
        CHECK_TRUE(gfx::theme_from_slug(gfx::theme_slug(id)) == id);
    }
}

TEST(theme_from_slug_unknown_is_default)
{
    CHECK_TRUE(gfx::theme_from_slug("") == gfx::ThemeId::RefinedSlate);
    CHECK_TRUE(gfx::theme_from_slug("not-a-theme") == gfx::ThemeId::RefinedSlate);
}

TEST(theme_slugs_are_unique)
{
    for (int i = 0; i < gfx::THEME_COUNT; ++i)
        for (int j = i + 1; j < gfx::THEME_COUNT; ++j)
            CHECK_FALSE(std::string_view(gfx::theme_slug(static_cast<gfx::ThemeId>(i))) ==
                        std::string_view(gfx::theme_slug(static_cast<gfx::ThemeId>(j))));
}

TEST(set_theme_switches_active_and_tokens)
{
    ThemeGuard guard;

    gfx::set_theme(gfx::ThemeId::Light);
    CHECK_TRUE(gfx::active_theme_id() == gfx::ThemeId::Light);
    // active_theme() and the theme::X reference tokens both reflect the switch.
    CHECK_TRUE(color_eq(gfx::active_theme().bg, gfx::theme_preset(gfx::ThemeId::Light).bg));
    CHECK_TRUE(color_eq(gfx::theme::BG, gfx::theme_preset(gfx::ThemeId::Light).bg));

    gfx::set_theme(gfx::ThemeId::Midnight);
    CHECK_TRUE(color_eq(gfx::theme::ACCENT, gfx::theme_preset(gfx::ThemeId::Midnight).accent));
}

TEST(set_theme_out_of_range_falls_back_to_default)
{
    ThemeGuard guard;
    gfx::set_theme(static_cast<gfx::ThemeId>(99));
    CHECK_TRUE(gfx::active_theme_id() == gfx::ThemeId::RefinedSlate);
}
