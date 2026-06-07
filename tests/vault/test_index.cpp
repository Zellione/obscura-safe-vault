#include "test_framework.h"

#include <string>
#include <vector>

#include "vault/index.h"

using vault::IndexNode;
using vault::ImageFormat;

static IndexNode make_image(const std::string& name, uint64_t off, uint64_t len)
{
    IndexNode n = IndexNode::image(name);
    n.meta.format       = ImageFormat::PNG;
    n.meta.width        = 640;
    n.meta.height       = 480;
    n.meta.orig_size    = 123456;
    n.meta.created_ts   = 1700000000ull;
    n.meta.data_offset  = off;
    n.meta.data_length  = len;
    n.meta.thumb_offset = off + 1000;
    n.meta.thumb_length = 256;
    return n;
}

static bool nodes_equal(const IndexNode& a, const IndexNode& b)
{
    if (a.type != b.type || a.name != b.name) return false;
    if (a.type == IndexNode::Type::Image) {
        const auto& x = a.meta;
        const auto& y = b.meta;
        return x.format == y.format && x.width == y.width && x.height == y.height &&
               x.orig_size == y.orig_size && x.created_ts == y.created_ts &&
               x.data_offset == y.data_offset && x.data_length == y.data_length &&
               x.thumb_offset == y.thumb_offset && x.thumb_length == y.thumb_length;
    }
    if (a.children.size() != b.children.size()) return false;
    for (size_t i = 0; i < a.children.size(); ++i)
        if (!nodes_equal(a.children[i], b.children[i])) return false;
    return true;
}

TEST(index_empty_root_roundtrips)
{
    IndexNode root = IndexNode::gallery("");

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    CHECK_TRUE(nodes_equal(root, out));
    CHECK_EQ(out.type, IndexNode::Type::Gallery);
    CHECK_TRUE(out.children.empty());
}

TEST(index_nested_tree_roundtrips)
{
    IndexNode root = IndexNode::gallery("");

    IndexNode a = IndexNode::gallery("vacation");
    IndexNode b = IndexNode::gallery("2024");
    b.children.push_back(make_image("beach.png", 4096, 9000));
    b.children.push_back(make_image("sunset.png", 13096, 8000));
    IndexNode c = IndexNode::gallery("2025");
    a.children.push_back(std::move(b));
    a.children.push_back(std::move(c));
    root.children.push_back(std::move(a));

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    CHECK_TRUE(nodes_equal(root, out));

    // Spot-check structure survived.
    REQUIRE(out.children.size() == 1);
    REQUIRE(out.children[0].children.size() == 2);
    REQUIRE(out.children[0].children[0].children.size() == 2);
    CHECK_EQ(out.children[0].children[0].children[0].name, std::string("beach.png"));
    CHECK_EQ(out.children[0].children[0].children[0].meta.data_offset, static_cast<uint64_t>(4096));
}

TEST(index_image_metadata_survives)
{
    IndexNode root = IndexNode::gallery("");
    root.children.push_back(make_image("photo.png", 777, 888));

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    REQUIRE(out.children.size() == 1);
    const auto& m = out.children[0].meta;
    CHECK_EQ(m.format, ImageFormat::PNG);
    CHECK_EQ(m.width, static_cast<uint32_t>(640));
    CHECK_EQ(m.height, static_cast<uint32_t>(480));
    CHECK_EQ(m.orig_size, static_cast<uint64_t>(123456));
    CHECK_EQ(m.created_ts, static_cast<uint64_t>(1700000000ull));
    CHECK_EQ(m.data_offset, static_cast<uint64_t>(777));
    CHECK_EQ(m.data_length, static_cast<uint64_t>(888));
    CHECK_EQ(m.thumb_offset, static_cast<uint64_t>(1777));
    CHECK_EQ(m.thumb_length, static_cast<uint64_t>(256));
}

TEST(index_deserialize_rejects_truncated_blob)
{
    IndexNode root = IndexNode::gallery("");
    root.children.push_back(make_image("photo.png", 1, 2));
    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    // Chop off the tail: deserialization must fail, not crash or read OOB.
    std::vector<uint8_t> truncated(blob.begin(), blob.begin() + blob.size() / 2);
    IndexNode out;
    CHECK_FALSE(vault::deserialize_index(truncated, out));
}

TEST(index_deserialize_rejects_empty_blob)
{
    IndexNode out;
    CHECK_FALSE(vault::deserialize_index(std::span<const uint8_t>{}, out));
}

TEST(index_deserialize_rejects_garbage)
{
    std::vector<uint8_t> garbage = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    IndexNode out;
    CHECK_FALSE(vault::deserialize_index(garbage, out));
}
