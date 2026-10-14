#include "test_framework.h"
#include "ui/import_tag_review.h"

TEST(import_tag_review_cleanup_nested_and_adjacent_annotations)
{
    CHECK_EQ(ui::strip_tag_annotations(" artist:Alice (alias <old>) <source> "), "artist:Alice");
    CHECK_EQ(ui::strip_tag_annotations("red<note>blue"), "red blue");
    CHECK_EQ(ui::strip_tag_annotations("a (one (two)) b"), "a b");
    CHECK_EQ(ui::strip_tag_annotations("café (été)"), "café");
    CHECK_EQ(ui::strip_tag_annotations("a [keep]"), "a [keep]");
}

TEST(import_tag_review_cleanup_unbalanced_delimiters)
{
    CHECK_EQ(ui::strip_tag_annotations("a (unfinished"), "a");
    CHECK_EQ(ui::strip_tag_annotations("a ) b > c"), "a b c");
    CHECK_EQ(ui::strip_tag_annotations("a (<mixed)> b"), "a b");
}

TEST(import_tag_review_selection_rename_replace_and_dedupe)
{
    ui::ImportTagReview review({"cat:Alice (alias)", "omit", "other", "duplicate"},
                               {"artist:Existing"});
    review.set_cleanup(true);
    CHECK_EQ(review.target(0), "cat:Alice");
    review.rows()[1].selected = false;
    CHECK(review.rename(0, "artist:New"));
    CHECK(review.replace(2, "ARTIST:existing"));
    CHECK(!review.replace(2, "missing"));
    CHECK(review.rename(3, "artist:new"));
    const auto result = review.selected_tags();
    REQUIRE(result.size() == 2);
    CHECK_EQ(result[0], "artist:New");
    CHECK_EQ(result[1], "artist:Existing");
    review.set_cleanup(false);
    CHECK_EQ(review.target(0), "artist:New");
}

TEST(import_tag_review_empty_cleanup_is_skipped_and_toggle_is_reversible)
{
    ui::ImportTagReview review({"(only)", "tag (note)"}, {});
    review.set_cleanup(true);
    CHECK_EQ(review.selected_tags().size(), size_t{1});
    CHECK(!review.rename(0, "   "));
    review.set_cleanup(false);
    CHECK_EQ(review.target(1), "tag (note)");
    CHECK_EQ(review.selected_tags().size(), size_t{2});
}

#include "ui/tag_dict_import.h"
#include "vault/index.h"

TEST(import_tag_review_dictionary_preserves_replacement_metadata_and_uses_first_collision)
{
    ui::TagDictParseResult parsed{.entries = {{"artist", "alias", "incoming"},
                                              {"", "skip", "skip"},
                                              {"", "new (note)", "first"},
                                              {"", "new", "second"}}};
    ui::ImportTagReview review({"artist:alias", "skip", "new (note)", "new"}, {"artist:Existing"});
    review.set_cleanup(true);
    CHECK(review.replace(0, "artist:Existing"));
    review.rows()[1].selected = false;
    vault::VaultSettings settings;
    vault::set_tag_description(settings, "artist:Existing", "keep");
    const auto sum = ui::apply_reviewed_tag_dict(settings, parsed, review);
    CHECK_EQ(vault::find_tag_description(settings, "artist:Existing"), "keep");
    CHECK_EQ(vault::find_tag_description(settings, "new"), "first");
    CHECK(vault::find_tag_description(settings, "skip").empty());
    CHECK_EQ(sum.descriptions_added, size_t{1});
}

#include "gfx/window.h"
#include "ui/import_tag_dialog.h"

TEST(import_tag_review_dialog_cancel_does_not_return_a_commit)
{
    gfx::Window window;
    ui::ImportTagDialog dialog(window);
    dialog.open(ui::ImportTagReview({"tag (alias)"}, {}));
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.key = SDLK_C;
    dialog.handle_event(key);
    key.key.key = SDLK_ESCAPE;
    dialog.handle_event(key);
    CHECK(!dialog.active());
    CHECK(!dialog.take_result().has_value());
}

TEST(import_tag_review_dialog_confirms_selection_and_cleanup)
{
    gfx::Window window;
    ui::ImportTagDialog dialog(window);
    dialog.open(ui::ImportTagReview({"tag (alias)", "skip"}, {}));
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    for (const auto code : {SDLK_C, SDLK_DOWN, SDLK_SPACE, SDLK_RETURN}) {
        key.key.key = code;
        dialog.handle_event(key);
    }
    auto result = dialog.take_result();
    REQUIRE(result.has_value());
    REQUIRE(result->selected_tags().size() == 1);
    CHECK_EQ(result->selected_tags().front(), "tag");
    CHECK(!dialog.take_result().has_value());
}

TEST(import_tag_review_rejects_invalid_names_and_skips_empty_cleaned_categories)
{
    ui::ImportTagReview review({"artist:(alias)", "artist:<old> Alice", "<category>:Alice"}, {});
    review.set_cleanup(true);
    CHECK(review.target(0).empty());
    CHECK_EQ(review.target(1), "artist:Alice");
    CHECK_EQ(review.target(2), "Alice");
    CHECK(!review.rename(0, "artist:"));
    CHECK(!review.rename(0, "bad\nname"));
    CHECK(!review.rename(4, "name"));
    CHECK(!review.replace(4, "name"));
    CHECK(review.target(4).empty());
}

TEST(import_tag_review_dialog_rename_and_existing_picker)
{
    gfx::Window window;
    ui::ImportTagDialog dialog(window);
    dialog.open(ui::ImportTagReview({"old", "alias"}, {"artist:Alice", "artist:Bob"}));
    auto key = [&](SDL_Keycode code) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        dialog.handle_event(event);
        event.type = SDL_EVENT_KEY_UP;
        dialog.handle_event(event);
    };
    auto text = [&](const char* value) {
        SDL_Event event{};
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = value;
        dialog.handle_event(event);
    };
    key(SDLK_R);
    text("renamed");
    key(SDLK_RETURN);
    key(SDLK_DOWN);
    key(SDLK_E);
    text("bob");
    key(SDLK_RETURN);
    key(SDLK_RETURN);
    auto result = dialog.take_result();
    REQUIRE(result.has_value());
    const auto tags = result->selected_tags();
    REQUIRE(tags.size() == 2);
    CHECK_EQ(tags[0], "renamed");
    CHECK_EQ(tags[1], "artist:Bob");
}

TEST(import_tag_review_layout_keeps_rows_above_controls)
{
    for (const float font : {16.0f, 28.0f, 40.0f}) {
        const auto layout = ui::import_tag_dialog_layout(font, 800, 600);
        CHECK(layout.box.x >= 0);
        CHECK(layout.box.y >= 0);
        CHECK(layout.list.y + layout.list.h <= layout.controls_y);
        CHECK(layout.list.h >= layout.pitch);
        CHECK(layout.list.x + layout.list.w <= layout.box.x + layout.box.w);
    }
}
