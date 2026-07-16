#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include "ui/video_repair.h"
#include "ui/zip_test_helpers.h"
#include "vault/vault.h"

#include <fstream>
#include <vector>

// Tests for ui::repair_unknown_video_metadata — the GalleryGrid::refresh()
// hook that self-heals videos imported before this build could decode their
// codec (Phase 40 bugfix: missing thumbnail/duration for such videos).

namespace {

std::vector<uint8_t> read_file(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

}  // namespace

namespace vault {
// Forward-declared here because a friend-only declaration (see vault.h) is
// not visible to qualified lookup from other translation units; defined in
// tests/vault/test_video.cpp (linked into this test binary).
void test_only_force_video_codec_unknown(Vault& v, std::string_view node_path);
}  // namespace vault

using ziptest::cleanup_dir;
using ziptest::fresh_dir;
using ziptest::make_vault;

TEST(repair_unknown_video_metadata_fills_in_previously_unknown_video)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    auto dir = fresh_dir("osv_video_repair_fills_in");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);
        vault::test_only_force_video_codec_unknown(v, "tiny.mp4");

        auto before = v.list("");
        REQUIRE(before.size() == static_cast<size_t>(1));
        REQUIRE(static_cast<int>(before[0]->vmeta.codec) ==
                static_cast<int>(vault::VideoCodec::Unknown));

        ui::repair_unknown_video_metadata(v, "", before);

        auto after = v.list("");
        REQUIRE(after.size() == static_cast<size_t>(1));
        CHECK(static_cast<int>(after[0]->vmeta.codec) !=
              static_cast<int>(vault::VideoCodec::Unknown));
        CHECK(after[0]->vmeta.duration_us > 0);
        CHECK(after[0]->vmeta.poster_length > 0);
    }
    cleanup_dir(dir);
}

TEST(repair_unknown_video_metadata_leaves_already_known_video_untouched)
{
    auto video_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!video_bytes.empty());

    auto dir = fresh_dir("osv_video_repair_noop_known");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.add_video("", video_bytes, "tiny.mp4", 4096) == vault::VaultResult::Ok);

        auto before = v.list("");
        REQUIRE(before.size() == static_cast<size_t>(1));
        const auto codec_before      = before[0]->vmeta.codec;
        const auto duration_before   = before[0]->vmeta.duration_us;
        const auto poster_len_before = before[0]->vmeta.poster_length;

        ui::repair_unknown_video_metadata(v, "", before);

        auto after = v.list("");
        REQUIRE(after.size() == static_cast<size_t>(1));
        CHECK(static_cast<int>(after[0]->vmeta.codec) == static_cast<int>(codec_before));
        CHECK(after[0]->vmeta.duration_us == duration_before);
        CHECK(after[0]->vmeta.poster_length == poster_len_before);
    }
    cleanup_dir(dir);
}

TEST(repair_unknown_video_metadata_ignores_non_video_children)
{
    auto img = std::vector<uint8_t>{0xFF, 0xD8, 0xFF};  // not a real image; just needs to not crash

    auto dir = fresh_dir("osv_video_repair_ignores_galleries");
    {
        vault::Vault v;
        make_vault(v, dir / "v.osv");
        REQUIRE(v.create_gallery("A") == vault::VaultResult::Ok);

        auto before = v.list("");
        REQUIRE(before.size() == static_cast<size_t>(1));
        REQUIRE(before[0]->is_gallery());

        // Must not crash or misbehave on a non-video child.
        ui::repair_unknown_video_metadata(v, "", before);

        auto after = v.list("");
        REQUIRE(after.size() == static_cast<size_t>(1));
        CHECK(after[0]->is_gallery());
    }
    cleanup_dir(dir);
}

#endif  // OSV_VENDORED_AV
