#include <string>
#include <vector>

#include "../test_framework.h"
#include "ui/tag_fields_form.h"
#include "vault/index.h"

TEST(templated_new_tag_category_detection)
{
    vault::VaultSettings s;
    s.categories = {{.name = crypto::SecureString("artist"), .swatch = 0, .fields = {crypto::SecureString("country")}},
                    {.name = crypto::SecureString("parody"), .swatch = 1, .fields = {}}};   // no template
    const std::vector<std::string> vocab = {"artist:known", "plain"};

    // New tag of a templated category → that category's name.
    CHECK_EQ(ui::templated_new_tag_category("artist:bob", vocab, s), std::string("artist"));
    // Existing tag (ci) → no prompt.
    CHECK(ui::templated_new_tag_category("ARTIST:KNOWN", vocab, s).empty());
    // New tag, category without template → no prompt.
    CHECK(ui::templated_new_tag_category("parody:thing", vocab, s).empty());
    // New tag, unconfigured prefix → no prompt.
    CHECK(ui::templated_new_tag_category("nope:thing", vocab, s).empty());
    // Uncategorised tag → no prompt.
    CHECK(ui::templated_new_tag_category("plain2", vocab, s).empty());
}
