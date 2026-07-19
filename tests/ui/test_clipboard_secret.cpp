#include "test_framework.h"

#include "ui/clipboard_secret.h"

TEST(clipboard_clears_when_current_matches_last_set)
{
    CHECK(ui::should_clear_clipboard(std::optional<std::string>{"hunter2"}, "hunter2"));
}

TEST(clipboard_does_not_clear_when_current_differs)
{
    // The user copied something else in the meantime — never clobber it.
    CHECK_FALSE(ui::should_clear_clipboard(std::optional<std::string>{"something else"}, "hunter2"));
}

TEST(clipboard_does_not_clear_when_query_failed)
{
    CHECK_FALSE(ui::should_clear_clipboard(std::optional<std::string>{}, "hunter2"));
}

TEST(clipboard_does_not_clear_when_nothing_was_ever_set)
{
    CHECK_FALSE(ui::should_clear_clipboard(std::optional<std::string>{""}, ""));
}
