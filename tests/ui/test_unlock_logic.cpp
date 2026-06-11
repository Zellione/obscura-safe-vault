#include "test_framework.h"

#include <string_view>

#include "ui/unlock_logic.h"

static std::span<const uint8_t> b(std::string_view s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

TEST(unlock_empty_password_rejected)
{
    auto d = ui::decide_submit(false, b(""), b(""));
    CHECK(d.action == ui::SubmitAction::None);
    CHECK(d.error != nullptr);
}

TEST(unlock_nonempty_is_unlock)
{
    auto d = ui::decide_submit(false, b("pw"), b(""));
    CHECK(d.action == ui::SubmitAction::Unlock);
    CHECK(d.error == nullptr);
}

TEST(create_mismatch_rejected)
{
    auto d = ui::decide_submit(true, b("pw1"), b("pw2"));
    CHECK(d.action == ui::SubmitAction::None);
    CHECK(d.error != nullptr);
}

TEST(create_match_is_create)
{
    auto d = ui::decide_submit(true, b("same"), b("same"));
    CHECK(d.action == ui::SubmitAction::Create);
    CHECK(d.error == nullptr);
}
