#include "test_framework.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "ui/export.h"
#include "vault/vault.h"
#include "platform/atomic_file.h"
#include "platform/path_utf8.h"

namespace fs = std::filesystem;

// Cheap Argon2 params so the suite stays fast (see tests/vault/test_vault.cpp).
static const crypto::KdfParams kExpKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

static std::span<const uint8_t> bytes(const std::string& s)
{
    return {reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

static std::vector<uint8_t> pattern(size_t n, uint8_t seed)
{
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 37 + seed);
    return v;
}

static std::vector<uint8_t> read_file(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// RAII unique temp directory.
struct TempDir {
    fs::path path;
    explicit TempDir(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_export_" + std::string(tag) + "_" + std::to_string(ctr++));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// RAII temp .osv path.
// Internal linkage: several test files each define their own `TempVault`
// with a DIFFERENT layout. At namespace scope those are one-definition-rule
// violations — the member functions are implicitly inline, so the linker keeps
// a single copy and silently discards the rest.
namespace {

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_exp_vault_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
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

}  // namespace

// --- export_one_media: decrypt -> write verbatim -> wipe scratch -----------

TEST(export_one_media_writes_verbatim_and_wipes_buffer)
{
    TempVault tv("one");
    TempDir   out("one");
    auto img = pattern(5000, 7);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "photo.png") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    auto f = platform::create_new_file_within(out.path, "photo.png");
    REQUIRE(f.has_value());
    CHECK_TRUE(f->display_path == out.path / "photo.png");

    crypto::SecureBytes scratch;
    REQUIRE(ui::export_one_media(v, *kids[0], std::move(*f), scratch) == vault::VaultResult::Ok);

    // File on disk is byte-identical to the originally-imported bytes.
    auto written = read_file(out.path / "photo.png");
    CHECK_BYTES_EQ(std::span<const uint8_t>(written), std::span<const uint8_t>(img));

    // The decrypted scratch buffer is wiped (all zero) after the write.
    REQUIRE(scratch.size() == img.size());
    bool all_zero = true;
    for (uint8_t b : scratch.as_span()) all_zero = all_zero && (b == 0);
    CHECK_TRUE(all_zero);
}

// A failed export write must still wipe the decrypted scratch buffer — the
// OSV-AUD-005 sink is atomic (never a truncating reopen), and the wipe is
// unconditional whether or not the bytes landed (invariant #1).
TEST(export_one_media_write_failure_wipes_scratch)
{
    TempVault tv("wr");
    TempDir   out("wr");
    auto img = pattern(2000, 11);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "photo.png") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    const fs::path dest = out.path / "photo.png";
    // An empty read-only file: the caller handed export_one_media an open handle
    // that CANNOT accept the write (read-only). Deterministic write failure
    // without needing a full-disk seam.
    {
        std::ofstream f(dest, std::ios::binary);
        f << "x";
    }
    std::FILE* ro = platform::fopen_path(dest, "rb");
    REQUIRE(ro != nullptr);
    platform::NewOutputFile ro_out{ro, dest};

    crypto::SecureBytes scratch;
    REQUIRE(ui::export_one_media(v, *kids[0], std::move(ro_out), scratch)
            == vault::VaultResult::IoError);

    // The decrypted bytes were still wiped on the failure path.
    REQUIRE(scratch.size() == img.size());
    bool all_zero = true;
    for (uint8_t b : scratch.as_span()) all_zero = all_zero && (b == 0);
    CHECK_TRUE(all_zero);
}

// --- export_images: batch, checksum, thumbnails-never, collisions ----------

TEST(export_images_are_byte_identical_and_skip_thumbnails)
{
    TempVault tv("batch");
    TempDir   out("batch");
    auto a = pattern(3000, 1);
    auto b = pattern(4096, 2);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", a, "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", b, "b.png") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 2);

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm);
    CHECK_EQ(sum.written, 2);
    CHECK_EQ(sum.failed, 0);

    // Exactly two files: the originals, never a thumbnail.
    int file_count = 0;
    for (auto& e : fs::directory_iterator(out.path)) { (void)e; ++file_count; }
    CHECK_EQ(file_count, 2);

    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "a.png")),
                   std::span<const uint8_t>(a));
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "b.png")),
                   std::span<const uint8_t>(b));
}

// export_images reports "N / M" progress: total set up front, done bumped per node.
TEST(export_images_reports_progress)
{
    TempVault tv("prog");
    TempDir   out("prog");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(1000, 1), "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(1000, 2), "b.png") == vault::VaultResult::Ok);
    auto kids = v.list("");

    vault::OpProgress prog;
    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm, &prog);
    CHECK_EQ(sum.written, 2);
    CHECK_EQ(prog.total.load(), 2);
    CHECK_EQ(prog.done.load(), 2);
}

// A cancel set before the loop stops it immediately: nothing is written.
TEST(export_images_cancel_writes_nothing)
{
    TempVault tv("cancel");
    TempDir   out("cancel");
    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(1000, 1), "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(1000, 2), "b.png") == vault::VaultResult::Ok);
    auto kids = v.list("");

    vault::OpProgress prog;
    prog.cancel.store(true);
    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm, &prog);
    CHECK_EQ(sum.written, 0);

    int file_count = 0;
    for (auto& e : fs::directory_iterator(out.path)) { (void)e; ++file_count; }
    CHECK_EQ(file_count, 0);
}

TEST(export_images_collision_suffixes_without_overwriting)
{
    TempVault tv("coll");
    TempDir   out("coll");
    auto img = pattern(2000, 9);

    // A pre-existing file occupies the natural destination name.
    {
        std::ofstream f(out.path / "a.png", std::ios::binary);
        const char* existing = "DO NOT OVERWRITE";
        f.write(existing, static_cast<std::streamsize>(std::strlen(existing)));
    }

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "a.png") == vault::VaultResult::Ok);
    auto kids = v.list("");

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm);
    CHECK_EQ(sum.written, 1);

    // The pre-existing file is untouched; the export lands beside it.
    CHECK_EQ(read_file(out.path / "a.png").size(), std::strlen("DO NOT OVERWRITE"));
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "a (1).png")),
                   std::span<const uint8_t>(img));
}

// OSV-AUD-005 / Phase 98: a symlink placed at the natural candidate (or that
// replaces a candidate between one export's suffix attempts) must never be
// followed into a truncating open. The atomic exclusive create sees the name
// taken and suffixes instead; the symlink target stays untouched.
#if !defined(_WIN32)
TEST(export_symlink_candidate_is_not_followed)
{
    TempVault tv("sym");
    TempDir   out("sym");
    TempDir   victim("sym_victim");
    auto img = pattern(1200, 13);

    // The natural output name is a symlink pointing OUTSIDE the destination.
    std::error_code ec;
    fs::create_symlink(victim.path / "stolen.bin", out.path / "a.png", ec);
    REQUIRE(!ec);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "a.png") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm);
    CHECK_EQ(sum.written, 1);
    CHECK_EQ(sum.failed, 0);

    // Nothing landed outside the chosen folder; the victim path was never
    // created, let alone truncated with decrypted bytes.
    CHECK_FALSE(fs::exists(victim.path / "stolen.bin"));

    // The decrypted bytes landed as a suffixed, real file inside dest_dir.
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "a (1).png")),
                   std::span<const uint8_t>(img));
    CHECK_TRUE(fs::is_symlink(out.path / "a.png", ec));
}
#endif

// --- Path traversal: a vault is UNTRUSTED INPUT ----------------------------
//
// Vaults are portable and shareable (vault manager, cross-vault transfer), and
// index.cpp deserialises IndexNode::name as opaque bytes with no validation. So
// a hostile .osv can carry a node named "../../.bashrc" or "/etc/cron.d/x", and
// `dest_dir / name` does NOT contain it — for an absolute name, operator/ throws
// the dest_dir away entirely. Export must never write outside the picked folder.
//
// These tests forge exactly that: import a legitimate image, then copy its node
// and rewrite only the *name*, leaving the chunk span intact — which is byte for
// byte the state a hostile vault would deserialise into.

TEST(export_path_within_accepts_a_file_in_the_destination)
{
    TempDir out("within");
    CHECK_TRUE(ui::export_path_within(out.path, out.path / "a.png"));
    CHECK_TRUE(ui::export_path_within(out.path, out.path / "sub" / "a.png"));
}

TEST(export_path_within_rejects_escapes)
{
    TempDir out("escape");
    CHECK_FALSE(ui::export_path_within(out.path, out.path / ".." / "a.png"));
    CHECK_FALSE(ui::export_path_within(out.path, out.path / ".." / ".." / "etc" / "passwd"));
    CHECK_FALSE(ui::export_path_within(out.path, fs::path("/etc/passwd")));
    CHECK_FALSE(ui::export_path_within(out.path, out.path));   // the dir itself is not a file in it
}

TEST(export_images_cannot_escape_dest_dir_via_relative_traversal)
{
    TempVault tv("trav");
    TempDir   out("trav");
    auto img = pattern(1500, 3);

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "ok.png") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    // Forge the hostile node: same chunk, traversal name.
    vault::IndexNode evil = *kids[0];
    evil.name = "../pwned.png";
    const vault::IndexNode* nodes[] = {&evil};

    auto sum = ui::export_images(v, nodes, out.path, ui::ExportConsent::Confirm);

    // Nothing lands in the parent directory.
    CHECK_FALSE(fs::exists(out.path.parent_path() / "pwned.png"));

    // The name was defanged and the file stayed inside the chosen folder.
    CHECK_EQ(sum.written, 1);
    CHECK_EQ(sum.failed, 0);
    int inside = 0;
    for (const auto& e : fs::directory_iterator(out.path)) {
        ++inside;
        CHECK_TRUE(ui::export_path_within(out.path, e.path()));
    }
    CHECK_EQ(inside, 1);
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / ".._pwned.png")),
                   std::span<const uint8_t>(img));
}

TEST(export_images_cannot_escape_dest_dir_via_absolute_name)
{
    TempVault tv("abs");
    TempDir   out("abs");
    TempDir   elsewhere("abs_victim");   // stands in for /etc, ~/.ssh, ...

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(900, 5), "ok.png") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    // An ABSOLUTE name: `dest_dir / name` would discard dest_dir completely.
    vault::IndexNode evil = *kids[0];
    evil.name = (elsewhere.path / "pwned.png").string();
    const vault::IndexNode* nodes[] = {&evil};

    auto sum = ui::export_images(v, nodes, out.path, ui::ExportConsent::Confirm);
    (void)sum;

    // The victim directory is untouched.
    CHECK_FALSE(fs::exists(elsewhere.path / "pwned.png"));
    int victim_files = 0;
    for (const auto& e : fs::directory_iterator(elsewhere.path)) { (void)e; ++victim_files; }
    CHECK_EQ(victim_files, 0);

    // Whatever was written stayed inside the destination.
    for (const auto& e : fs::directory_iterator(out.path))
        CHECK_TRUE(ui::export_path_within(out.path, e.path()));
}

TEST(export_images_declining_writes_nothing)
{
    TempVault tv("decline");
    TempDir   out("decline");

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", pattern(100, 1), "a.png") == vault::VaultResult::Ok);
    auto kids = v.list("");

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Cancel);
    CHECK_EQ(sum.written, 0);

    int file_count = 0;
    for (auto& e : fs::directory_iterator(out.path)) { (void)e; ++file_count; }
    CHECK_EQ(file_count, 0);
}

// --- Phase 53: videos are selectable, so export must handle them -----------
//
// Until Phase 53 a video could not be selected at all (Space refused it) and
// export_one_image rejected any non-image outright. Ctrl+A now selects videos,
// so a selection can legitimately contain one.

TEST(export_writes_a_video_original_byte_identical)
{
    TempVault tv("vid");
    TempDir   out("vid");
    // add_video probes the container (media::probe_video) and rejects anything
    // that is not a real video, so this needs the actual fixture.
    auto clip = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!clip.empty());

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", clip, "clip.mp4") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm);
    CHECK_EQ(sum.written, 1);
    CHECK_EQ(sum.failed, 0);
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "clip.mp4")),
                   std::span<const uint8_t>(clip));
}

TEST(export_handles_a_mixed_image_and_video_selection)
{
    TempVault tv("mixed");
    TempDir   out("mixed");
    auto img  = pattern(2048, 3);
    auto clip = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
    REQUIRE(!clip.empty());

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.add_image("", img, "a.png") == vault::VaultResult::Ok);
    REQUIRE(v.add_video("", clip, "b.mp4") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 2);

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm);
    CHECK_EQ(sum.written, 2);
    CHECK_EQ(sum.failed, 0);
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "a.png")),
                   std::span<const uint8_t>(img));
    CHECK_BYTES_EQ(std::span<const uint8_t>(read_file(out.path / "b.mp4")),
                   std::span<const uint8_t>(clip));
}

TEST(export_still_refuses_a_gallery_node)
{
    // Widening to videos must not accidentally let a gallery through: it has no
    // stored bytes of its own, and silently "exporting" one would be a lie.
    TempVault tv("gal");
    TempDir   out("gal");

    vault::Vault v;
    REQUIRE(vault::Vault::create(tv.str(), bytes("pw"), {}, kExpKdf, v)
            == vault::VaultResult::Ok);
    REQUIRE(v.create_gallery("sub") == vault::VaultResult::Ok);
    auto kids = v.list("");
    REQUIRE(kids.size() == 1);

    auto sum = ui::export_images(v, kids, out.path, ui::ExportConsent::Confirm);
    CHECK_EQ(sum.written, 0);
    CHECK_EQ(sum.failed, 1);
}
