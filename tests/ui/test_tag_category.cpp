// Phase 49: pure tag → (display text, colour swatch) resolution. A tag whose
// prefix matches a CONFIGURED category shows the bare name; anything else shows
// verbatim, so "12:30" and "Re:Zero" are never mangled.

#include "test_framework.h"
#include "ui/tag_category.h"

#include <string>
#include <vector>

using vault::TagCategory;

namespace {

const std::vector<TagCategory> CATS = {
    {.name = "artist", .swatch = 3}, {.name = "parody", .swatch = 7}, {.name = "female", .swatch = 9},
};

} // namespace

TEST(tag_category_strips_configured_prefix)
{
    const auto d = ui::resolve_tag("artist:Kaguya", CATS);
    CHECK(d.text == "Kaguya");
    CHECK_EQ(d.swatch, 3);
}

TEST(tag_category_prefix_match_is_case_insensitive)
{
    const auto d = ui::resolve_tag("ARTIST:Kaguya", CATS);
    CHECK(d.text == "Kaguya");
    CHECK_EQ(d.swatch, 3);
}

TEST(tag_category_unconfigured_prefix_is_verbatim)
{
    const auto d = ui::resolve_tag("studio:Trigger", CATS);
    CHECK(d.text == "studio:Trigger");
    CHECK(d.swatch < 0);
}

TEST(tag_category_time_and_title_colons_survive)
{
    const auto t = ui::resolve_tag("12:30", CATS);
    CHECK(t.text == "12:30");
    CHECK(t.swatch < 0);

    const auto z = ui::resolve_tag("Re:Zero", CATS);
    CHECK(z.text == "Re:Zero");
    CHECK(z.swatch < 0);
}

TEST(tag_category_bare_tag_is_verbatim)
{
    const auto d = ui::resolve_tag("ponytail", CATS);
    CHECK(d.text == "ponytail");
    CHECK(d.swatch < 0);
}

TEST(tag_category_empty_suffix_is_verbatim)
{
    const auto d = ui::resolve_tag("artist:", CATS);
    CHECK(d.text == "artist:");
    CHECK(d.swatch < 0);
}

TEST(tag_category_splits_on_the_first_colon_only)
{
    const auto d = ui::resolve_tag("parody:Fate:Grand Order", CATS);
    CHECK(d.text == "Fate:Grand Order");
    CHECK_EQ(d.swatch, 7);
}

TEST(tag_category_empty_inputs_are_safe)
{
    const auto e = ui::resolve_tag("", CATS);
    CHECK(e.text.empty());
    CHECK(e.swatch < 0);

    const auto none = ui::resolve_tag("artist:Kaguya", {});
    CHECK(none.text == "artist:Kaguya");
    CHECK(none.swatch < 0);
}
