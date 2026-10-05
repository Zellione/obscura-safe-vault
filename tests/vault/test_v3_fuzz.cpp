#include "test_framework.h"

#include "vault/v3_db.h"
#include "vault/v3_fs.h"
#include "vault/v3_header.h"
#include "vault/v3_object_store.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unistd.h>

namespace {
namespace fs = std::filesystem;
using namespace vault::v3;

constexpr Id ROOT{1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
constexpr Id NODE{2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
constexpr Id VAULT{3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3};
constexpr std::array<uint8_t, crypto::KEY_SIZE> KEY{1, 2, 3, 4, 5, 6, 7, 8};

struct TempDir {
    fs::path path;
    TempDir()
    {
        std::array<char, 64> pattern{};
        std::snprintf(pattern.data(), pattern.size(), "/tmp/osv-v3-fuzz-XXXXXX");
        if (const char* made = ::mkdtemp(pattern.data())) path = made;
    }
    ~TempDir()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }
};

Id id_for(uint32_t value)
{
    Id id{};
    id[0] = 0xa5;
    for (size_t i = 0; i < sizeof(value); ++i)
        id[1 + i] = static_cast<uint8_t>(value >> (i * 8));
    return id;
}
}  // namespace

TEST(v3_fuzz_header_parser_is_total_and_canonicalizes_every_accepted_input)
{
    std::mt19937_64 random{0x114'0bad'cafeULL};
    for (size_t sample = 0; sample < 4096; ++sample) {
        std::array<uint8_t, V3_HEADER_SIZE> bytes{};
        for (auto& byte : bytes)
            byte = static_cast<uint8_t>(random());
        V3Header parsed;
        if (parse_v3_header(bytes, parsed) != HeaderStatus::Ok) continue;
        const auto canonical = serialize_v3_header(parsed);
        V3Header reparsed;
        CHECK(parse_v3_header(canonical, reparsed) == HeaderStatus::Ok);
    }
}

TEST(v3_fuzz_object_path_parser_round_trips_only_canonical_names)
{
    std::mt19937 random{0x114f5};
    for (size_t sample = 0; sample < 4096; ++sample) {
        std::string candidate;
        candidate.resize(random() % 96);
        for (char& ch : candidate)
            ch = static_cast<char>(random() & 0x7f);
        const auto parsed = parse_object_relative_path(candidate);
        if (parsed) CHECK_EQ(object_relative_path(*parsed), candidate);
    }
    for (uint32_t value = 1; value < 1024; ++value) {
        const auto id = id_for(value);
        CHECK(parse_object_relative_path(object_relative_path(id)) == id);
    }
}

TEST(v3_fuzz_object_parser_never_releases_changed_plaintext)
{
    TempDir temp;
    auto root = VaultRoot::create(temp.path / "vault.osv");
    REQUIRE(root.has_value());
    const std::array<uint8_t, 257> plain{0x6d};
    const ObjectWriteRequest request{VAULT, NODE, ObjectRole::OriginalImage, 1, 64};
    const auto written = write_object(*root, KEY, request, plain);
    REQUIRE(written.status == ObjectStatus::Ok);
    const auto path = temp.path / "vault.osv" / object_relative_path(written.info.object_id);
    std::ifstream input(path, std::ios::binary);
    const std::vector<uint8_t> original{std::istreambuf_iterator<char>(input), {}};
    REQUIRE(!original.empty());

    std::mt19937 random{0x1140b1};
    for (size_t sample = 0; sample < 256; ++sample) {
        auto changed = original;
        changed[random() % changed.size()] ^= static_cast<uint8_t>(1U << (random() % 8));
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(changed.data()),
                         static_cast<std::streamsize>(changed.size()));
        }
        auto opened = ObjectReader::open(*root, KEY, written.info);
        if (opened.status != ObjectStatus::Ok) continue;
        crypto::SecureBytes decoded;
        const auto status = opened.reader->read_all(decoded);
        CHECK(status != ObjectStatus::Ok || std::ranges::equal(decoded.as_span(), plain));
    }
}

TEST(v3_fuzz_sql_facing_values_are_rejected_or_round_trip_without_truncation)
{
    TempDir temp;
    auto opened = Database::create(temp.path / "vault.db", KEY, ROOT);
    REQUIRE(opened.database.has_value());
    std::mt19937 random{0x114db};
    for (uint32_t sample = 1; sample <= 256; ++sample) {
        std::string name;
        name.resize(1 + random() % 300);
        for (char& ch : name)
            ch = static_cast<char>(1 + random() % 255);
        NodeRecord node;
        node.node_id = id_for(sample);
        node.parent_id = ROOT;
        node.type = NodeType::Image;
        node.display_name = name;
        node.sibling_order = sample;
        const auto status = opened.database->insert_node(node);
        if (name.size() > 255) {
            CHECK(status == DbStatus::InvalidArgument);
            continue;
        }
        REQUIRE(status == DbStatus::Ok);
        const auto found = opened.database->find_node(node.node_id);
        REQUIRE(found.status == DbStatus::Ok && found.value.has_value());
        CHECK_EQ(found.value->display_name.view(), std::string_view{name});
    }
}
