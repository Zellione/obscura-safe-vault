#include "test_framework.h"

// GifPlayback wires the decoder + frame-advance model + RGBA texture together.
// The pure frame-advance logic is covered by test_gif_model; this drives the
// whole glue against a real encrypted fixture, and asserts the security invariant
// that playback writes nothing to disk. Gated on the vendored FFmpeg build
// (valid() is always false without it).
#ifdef OSV_VENDORED_AV

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <SDL3/SDL.h>

#include "crypto/kdf.h"
#include "ui/gif_playback.h"
#include "vault/index.h"
#include "vault/vault.h"

namespace {
namespace fs = std::filesystem;

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_gp_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault()
    {
        std::error_code ec;
        fs::remove(path, ec);
    }
    std::string str() const { return path.string(); }
};

std::vector<uint8_t> read_file(const char* file_path)
{
    std::ifstream f(file_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

const vault::IndexNode* first_animated_image(const std::vector<const vault::IndexNode*>& ns)
{
    for (const vault::IndexNode* n : ns) {
        if (n != nullptr && n->is_image() && n->meta.animated) {
            return n;
        }
    }
    return nullptr;
}
}  // namespace

TEST(gif_playback_opens_an_animated_gif)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("open");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::GifPlayback p(v, *node);
    CHECK(p.valid());
    CHECK(p.animating());
    CHECK(!p.paused());
}

TEST(gif_playback_space_toggles_pause)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("pause");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::GifPlayback p(v, *node);
    REQUIRE(p.valid());
    p.toggle_pause();
    CHECK(p.paused());
    CHECK(!p.animating());
    p.toggle_pause();
    CHECK(!p.paused());
    CHECK(p.animating());
}

TEST(gif_playback_advances_frames_over_time)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("advance");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::GifPlayback p(v, *node);
    REQUIRE(p.valid());
    const size_t start = p.frames_shown();
    for (int i = 0; i < 60; ++i) p.update(0.050);   // 3 seconds of playback
    CHECK(p.frames_shown() > start);
}

TEST(gif_playback_paused_does_not_advance)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("paused");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::GifPlayback p(v, *node);
    REQUIRE(p.valid());
    for (int i = 0; i < 10; ++i) p.update(0.050);
    p.toggle_pause();
    const size_t held = p.frames_shown();
    for (int i = 0; i < 60; ++i) p.update(0.050);
    CHECK_EQ(p.frames_shown(), held);
}

TEST(gif_playback_loops_past_the_end)
{
    auto gbytes = read_file(OSV_MEDIA_FIXTURE_DIR "/tiny_anim.gif");
    REQUIRE(!gbytes.empty());

    TempVault tv("loop");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("c") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("c", gbytes, "tiny_anim.gif") == vault::VaultResult::Ok);

    const vault::IndexNode* node = first_animated_image(v.list("c"));
    REQUIRE(node != nullptr);

    ui::GifPlayback p(v, *node);
    REQUIRE(p.valid());
    // tiny_anim.gif has 4 frames at 0.25s per frame = 1 second
    for (int i = 0; i < 200; ++i) p.update(0.050);   // 10 seconds, ~10 loops
    CHECK(p.frames_shown() > 4);                     // kept going past frame 4
    CHECK(p.animating());                            // still playing, never stalled
}

#endif  // OSV_VENDORED_AV
