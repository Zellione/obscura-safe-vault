#include "test_framework.h"

#include <algorithm>
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
    if (a.tags != b.tags) return false;
    if (a.favorite != b.favorite) return false;
    if (a.sort_key != b.sort_key) return false;
    if (a.type == IndexNode::Type::Image) {
        const auto& x = a.meta;
        const auto& y = b.meta;
        return x.format == y.format && x.width == y.width && x.height == y.height &&
               x.orig_size == y.orig_size && x.created_ts == y.created_ts &&
               x.data_offset == y.data_offset && x.data_length == y.data_length &&
               x.thumb_offset == y.thumb_offset && x.thumb_length == y.thumb_length &&
               x.animated == y.animated;
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

TEST(index_image_tags_roundtrip)
{
    IndexNode root = IndexNode::gallery("");
    IndexNode img = make_image("photo.png", 777, 888);
    img.tags.push_back("vacation");
    img.tags.push_back("beach");
    img.tags.push_back("2024");
    root.children.push_back(std::move(img));

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    REQUIRE(out.children.size() == 1);
    const auto& tags = out.children[0].tags;
    REQUIRE(tags.size() == 3);
    CHECK_EQ(tags[0], std::string("vacation"));
    CHECK_EQ(tags[1], std::string("beach"));
    CHECK_EQ(tags[2], std::string("2024"));
}

TEST(index_gallery_tags_roundtrip)
{
    IndexNode root = IndexNode::gallery("");
    IndexNode gal = IndexNode::gallery("my_gallery");
    gal.tags.push_back("important");
    gal.tags.push_back("archived");
    root.children.push_back(std::move(gal));

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    REQUIRE(out.children.size() == 1);
    const auto& tags = out.children[0].tags;
    REQUIRE(tags.size() == 2);
    CHECK_EQ(tags[0], std::string("important"));
    CHECK_EQ(tags[1], std::string("archived"));
}

TEST(index_nested_tree_with_tags_roundtrips)
{
    IndexNode root = IndexNode::gallery("");
    root.tags.push_back("root_tag");

    IndexNode a = IndexNode::gallery("vacation");
    a.tags.push_back("gal_tag");
    IndexNode b = IndexNode::gallery("2024");
    b.tags.push_back("year");
    b.children.push_back(make_image("beach.png", 4096, 9000));
    b.children[0].tags.push_back("img_tag");
    a.children.push_back(std::move(b));
    root.children.push_back(std::move(a));

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    CHECK_TRUE(nodes_equal(root, out));
}

TEST(index_deserialize_v1_blob_has_empty_tags)
{
    // Hand-crafted version 1 blob: magic 0x01, then a gallery node
    // type=0x00, name_len=0x00 0x00, 0 children (u32=0x00 0x00 0x00 0x00)
    std::vector<uint8_t> v1_blob = {
        0x01,                          // INDEX_VERSION = 1
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0x00, 0x00, 0x00, 0x00        // child_count = 0
    };

    IndexNode out;
    REQUIRE(vault::deserialize_index(v1_blob, out));
    CHECK_EQ(out.type, IndexNode::Type::Gallery);
    CHECK_EQ(out.name, std::string(""));
    CHECK_TRUE(out.tags.empty());
    CHECK_TRUE(out.children.empty());
}

TEST(index_deserialize_v1_gallery_with_v1_image_child)
{
    // Hand-crafted version 1 blob: gallery with one image child
    std::vector<uint8_t> v1_blob = {
        0x01,                                      // INDEX_VERSION = 1
        0x00,                                      // type = Gallery
        0x00, 0x00,                                // name_len = 0
        0x01, 0x00, 0x00, 0x00,                   // child_count = 1
        // Child image node:
        0x01,                                      // type = Image
        0x04, 0x00,                                // name_len = 4
        'p', 'i', 'c', '.',                        // name = "pic."
        0x01,                                      // format = PNG
        0x00, 0x00, 0x00, 0x00,                   // width = 0
        0x00, 0x00, 0x00, 0x00,                   // height = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // orig_size
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // created_ts
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // data_offset
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // data_length
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // thumb_offset
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // thumb_length
    };

    IndexNode out;
    REQUIRE(vault::deserialize_index(v1_blob, out));
    CHECK_EQ(out.type, IndexNode::Type::Gallery);
    CHECK_TRUE(out.tags.empty());
    REQUIRE(out.children.size() == 1);
    CHECK_EQ(out.children[0].type, IndexNode::Type::Image);
    CHECK_EQ(out.children[0].name, std::string("pic."));
    CHECK_TRUE(out.children[0].tags.empty());
}

TEST(index_deserialize_rejects_future_version)
{
    // Blob with version 0xFF (unknown future version)
    std::vector<uint8_t> future_blob = {
        0xFF,                          // version = 255 (unknown)
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0x00, 0x00, 0x00, 0x00        // child_count = 0
    };

    IndexNode out;
    CHECK_FALSE(vault::deserialize_index(future_blob, out));
}

TEST(index_deserialize_rejects_hostile_tag_count)
{
    // Blob claiming massive tag count but with truncated tag data
    std::vector<uint8_t> hostile_blob = {
        0x02,                          // version = 2 (tags version)
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0xFF, 0xFF,                    // tag_count = 65535 (but no tag data follows)
        0x00, 0x00, 0x00, 0x00        // child_count = 0 (never reached)
    };

    IndexNode out;
    CHECK_FALSE(vault::deserialize_index(hostile_blob, out));
}

// --- Phase 13: favorite flag ----------------------------------------------

TEST(index_image_favorite_roundtrips)
{
    IndexNode root = IndexNode::gallery("");
    IndexNode img = make_image("photo.png", 12, 34);
    img.favorite = true;
    root.children.push_back(std::move(img));

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    REQUIRE(out.children.size() == 1);
    CHECK_TRUE(out.children[0].favorite);
}

TEST(index_gallery_favorite_roundtrips)
{
    IndexNode root = IndexNode::gallery("");
    IndexNode gal = IndexNode::gallery("starred");
    gal.favorite = true;
    IndexNode plain = IndexNode::gallery("plain");  // favorite stays false
    root.children.push_back(std::move(gal));
    root.children.push_back(std::move(plain));

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    REQUIRE(out.children.size() == 2);
    CHECK_TRUE(out.children[0].favorite);
    CHECK_FALSE(out.children[1].favorite);
}

TEST(index_favorite_combines_with_tags_in_nested_tree)
{
    IndexNode root = IndexNode::gallery("");
    IndexNode a = IndexNode::gallery("trip");
    a.favorite = true;
    a.tags.push_back("gal_tag");
    IndexNode img = make_image("beach.png", 4096, 9000);
    img.favorite = true;
    img.tags.push_back("img_tag");
    a.children.push_back(std::move(img));
    root.children.push_back(std::move(a));

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    CHECK_TRUE(nodes_equal(root, out));
}

TEST(index_deserialize_v2_blob_has_no_favorite)
{
    // Hand-crafted version 2 blob (tags, but no favorite byte): a gallery with
    // zero tags and zero children. A v2 reader/writer never wrote a favorite
    // byte, so it must read back as not-favorited under the v3 parser.
    std::vector<uint8_t> v2_blob = {
        0x02,                          // INDEX_VERSION = 2
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0x00, 0x00,                    // tag_count = 0
        0x00, 0x00, 0x00, 0x00        // child_count = 0
    };

    IndexNode out;
    REQUIRE(vault::deserialize_index(v2_blob, out));
    CHECK_EQ(out.type, IndexNode::Type::Gallery);
    CHECK_TRUE(out.tags.empty());
    CHECK_FALSE(out.favorite);
    CHECK_TRUE(out.children.empty());
}

TEST(index_deserialize_v1_blob_has_no_favorite)
{
    // Pre-tags, pre-favorites blob still opens with favorite=false.
    std::vector<uint8_t> v1_blob = {
        0x01,                          // INDEX_VERSION = 1
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0x00, 0x00, 0x00, 0x00        // child_count = 0
    };

    IndexNode out;
    REQUIRE(vault::deserialize_index(v1_blob, out));
    CHECK_FALSE(out.favorite);
}

// --- Phase 15 PR2: Video nodes (index v4) ----------------------------------

TEST(index_v4_video_node_round_trip)
{
    using namespace vault;
    IndexNode root = IndexNode::gallery("root");
    IndexNode vid  = IndexNode::video("clip.mp4");
    vid.vmeta.container   = VideoContainer::MP4;
    vid.vmeta.codec       = VideoCodec::H264;
    vid.vmeta.width       = 1920;
    vid.vmeta.height      = 1080;
    vid.vmeta.duration_us = 5'000'000;
    vid.vmeta.orig_size   = 3'000'000;
    vid.vmeta.created_ts  = 1718000000;
    vid.vmeta.chunk_size  = 1u << 20;
    vid.vmeta.chunks      = { {100, 1048616}, {1049000, 1048616}, {2098000, 902800} };
    vid.vmeta.poster_offset = 0;
    vid.vmeta.poster_length = 0;
    vid.tags = {"beach"};
    vid.favorite = true;
    root.children.push_back(vid);

    std::vector<uint8_t> blob;
    serialize_index(root, blob);
    CHECK(blob[0] == INDEX_VERSION);   // current serialised version byte

    IndexNode back;
    CHECK(deserialize_index(blob, back));
    REQUIRE(back.children.size() == 1);
    const IndexNode& v = back.children[0];
    CHECK(v.is_video());
    CHECK(v.is_media());
    CHECK_FALSE(v.is_image());
    CHECK_EQ(static_cast<int>(v.vmeta.container), static_cast<int>(VideoContainer::MP4));
    CHECK_EQ(static_cast<int>(v.vmeta.codec), static_cast<int>(VideoCodec::H264));
    CHECK_EQ(v.vmeta.width, 1920u);
    CHECK_EQ(v.vmeta.height, 1080u);
    CHECK_EQ(v.vmeta.duration_us, 5'000'000ull);
    CHECK_EQ(v.vmeta.orig_size, 3'000'000ull);
    CHECK_EQ(v.vmeta.chunk_size, 1u << 20);
    REQUIRE(v.vmeta.chunks.size() == 3);
    CHECK_EQ(v.vmeta.chunks[2].offset, 2098000ull);
    CHECK_EQ(v.vmeta.chunks[2].length, 902800ull);
    CHECK(v.favorite);
    REQUIRE(v.tags.size() == 1);
    CHECK(v.tags[0] == "beach");
}

TEST(index_v4_rejects_excessive_chunk_count)
{
    using namespace vault;
    // Hand-craft a v4 blob whose video node claims a huge chunk_count.
    // version(4) | type(2=Video) | name_len(0) | tag_count(0) | favorite(0)
    //   | container(0) | codec(0) | w(0) | h(0) | duration(0) | orig(0)
    //   | created(0) | chunk_size(0) | chunk_count(0xFFFFFFFF)
    std::vector<uint8_t> blob = {4, 2, 0,0, 0,0, 0, 0, 0};
    blob.insert(blob.end(), 4+4+8+8+8+4, 0);        // w,h,duration,orig,created,chunk_size
    blob.insert(blob.end(), {0xFF,0xFF,0xFF,0xFF}); // chunk_count = 4 billion
    IndexNode out;
    CHECK_FALSE(deserialize_index(blob, out));       // must reject, not allocate
}

// --- Phase 18: vault-global saved-searches block (index v5) -----------------

TEST(index_v5_saved_searches_round_trip)
{
    using namespace vault;
    IndexNode root = IndexNode::gallery("");
    root.children.push_back(make_image("a.png", 10, 20));

    std::vector<SavedSearch> searches = {
        SavedSearch{"cats", {0x01, 0x02, 0x03}},
        SavedSearch{"vacation 2024", {0xAA, 0xBB}},
    };

    std::vector<uint8_t> blob;
    serialize_index(root, searches, blob);
    CHECK(blob[0] == INDEX_VERSION);   // version byte is 5

    IndexNode out;
    std::vector<SavedSearch> back;
    REQUIRE(deserialize_index(blob, out, back));
    REQUIRE(out.children.size() == 1);
    REQUIRE(back.size() == 2);
    CHECK_EQ(back[0].name, std::string("cats"));
    CHECK_BYTES_EQ(std::span<const uint8_t>(back[0].query),
                   std::span<const uint8_t>(searches[0].query));
    CHECK_EQ(back[1].name, std::string("vacation 2024"));
    CHECK_BYTES_EQ(std::span<const uint8_t>(back[1].query),
                   std::span<const uint8_t>(searches[1].query));
}

TEST(index_two_arg_serialize_emits_v5_with_no_saved_searches)
{
    using namespace vault;
    IndexNode root = IndexNode::gallery("");
    std::vector<uint8_t> blob;
    serialize_index(root, blob);            // legacy 2-arg path
    CHECK(blob[0] == INDEX_VERSION);

    IndexNode out;
    std::vector<SavedSearch> back;
    REQUIRE(deserialize_index(blob, out, back));
    CHECK_TRUE(back.empty());
}

TEST(index_pre_v5_blob_reads_with_no_saved_searches)
{
    using namespace vault;
    // A hand-crafted v4 blob (gallery, no children) carries no saved-searches
    // block; the v5 reader must surface an empty list rather than failing.
    std::vector<uint8_t> v4_blob = {
        0x04,                          // INDEX_VERSION = 4
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0x00, 0x00,                    // tag_count = 0
        0x00,                          // favorite = 0
        0x00, 0x00, 0x00, 0x00,        // child_count = 0
    };

    IndexNode out;
    std::vector<SavedSearch> back;
    REQUIRE(deserialize_index(v4_blob, out, back));
    CHECK_TRUE(back.empty());
}

TEST(index_deserialize_rejects_hostile_saved_search_count)
{
    using namespace vault;
    // v5 gallery with zero children, then a saved-search count of 65535 but no
    // entries following — must reject without a huge allocation.
    std::vector<uint8_t> blob = {
        0x05,                          // INDEX_VERSION = 5
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0x00, 0x00,                    // tag_count = 0
        0x00,                          // favorite = 0
        0x00, 0x00, 0x00, 0x00,        // child_count = 0
        0xFF, 0xFF,                    // saved_search_count = 65535 (no data follows)
    };

    IndexNode out;
    std::vector<SavedSearch> back;
    CHECK_FALSE(deserialize_index(blob, out, back));
}

// --- Phase 37: per-gallery sort_key ----------------------------------------

TEST(index_gallery_sort_key_roundtrips)
{
    using namespace vault;
    IndexNode root = IndexNode::gallery("");
    IndexNode gal = IndexNode::gallery("trip");
    gal.sort_key = SortKey::NameDesc;
    IndexNode plain = IndexNode::gallery("plain");  // sort_key stays Manual
    root.children.push_back(std::move(gal));
    root.children.push_back(std::move(plain));

    std::vector<uint8_t> blob;
    serialize_index(root, blob);

    IndexNode out;
    REQUIRE(deserialize_index(blob, out));
    REQUIRE(out.children.size() == 2);
    CHECK_EQ(out.children[0].sort_key, SortKey::NameDesc);
    CHECK_EQ(out.children[1].sort_key, SortKey::Default);
}

TEST(index_deserialize_v5_blob_has_manual_sort_key)
{
    using namespace vault;
    // Hand-crafted v5 blob (favorite byte present, saved-searches block present,
    // but no sort_key byte — v5 writers never wrote one). Must read back Manual.
    std::vector<uint8_t> v5_blob = {
        0x05,                          // INDEX_VERSION = 5
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0x00, 0x00,                    // tag_count = 0
        0x00,                          // favorite = 0
        0x00, 0x00, 0x00, 0x00,        // child_count = 0
        0x00, 0x00,                    // saved_search_count = 0
    };

    IndexNode out;
    std::vector<SavedSearch> back;
    REQUIRE(deserialize_index(v5_blob, out, back));
    CHECK_EQ(out.sort_key, SortKey::Default);
}

TEST(index_deserialize_rejects_out_of_range_sort_key)
{
    using namespace vault;
    // v6 blob with sort_key = 7 (only 0..6 are defined) — must be rejected, not
    // clamped, mirroring how an unknown node `type` byte is already rejected.
    std::vector<uint8_t> blob = {
        0x06,                          // INDEX_VERSION = 6
        0x00,                          // type = Gallery
        0x00, 0x00,                    // name_len = 0
        0x00, 0x00,                    // tag_count = 0
        0x00,                          // favorite = 0
        0x07,                          // sort_key = 7 (out of range)
        0x00, 0x00, 0x00, 0x00,        // child_count = 0 (never reached)
    };

    IndexNode out;
    CHECK_FALSE(deserialize_index(blob, out));
}

// --- Phase 47: per-image animated flag (index v7) ----------------------------

TEST(index_animated_flag_round_trips)
{
    IndexNode root = IndexNode::gallery("root");
    IndexNode gif  = IndexNode::image("loop.gif");
    gif.meta.format   = ImageFormat::GIF;
    gif.meta.animated = true;
    root.children.push_back(gif);

    IndexNode still = IndexNode::image("photo.jpg");
    still.meta.format = ImageFormat::JPEG;
    root.children.push_back(still);

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);

    IndexNode out;
    REQUIRE(vault::deserialize_index(blob, out));
    REQUIRE(out.children.size() == size_t{2});
    CHECK(out.children[0].meta.animated);
    CHECK(!out.children[1].meta.animated);
}

TEST(index_version_is_nine)
{
    IndexNode root = IndexNode::gallery("root");
    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);
    REQUIRE(!blob.empty());
    CHECK_EQ(blob[0], uint8_t{9});
}

TEST(index_v6_blob_reads_animated_as_false)
{
    // Hand-crafted v6 blob: gallery with one image child.
    // Structure: version(1) | root_type(1) | root_name_len(2) | root_tags(2) |
    //   root_favorite(1) | root_sort_key(1) | child_count(4) | child_type(1) |
    //   child_name_len(2) | child_name(9) | child_tags(2) | child_favorite(1) |
    //   child_sort_key(1) | format(1) | width(4) | height(4) | orig_size(8) |
    //   created_ts(8) | data_offset(8) | data_length(8) | thumb_offset(8) |
    //   thumb_length(8) | saved_searches_count(2)
    std::vector<uint8_t> v6_blob = {
        0x06,                                    // version = 6
        0x00,                                    // root type = Gallery
        0x04, 0x00,                              // root name_len = 4
        'r', 'o', 'o', 't',                      // root name = "root"
        0x00, 0x00,                              // root tag_count = 0
        0x00,                                    // root favorite = 0
        0x00,                                    // root sort_key = 0
        0x01, 0x00, 0x00, 0x00,                  // child_count = 1
        // Child image node:
        0x01,                                    // child type = Image
        0x09, 0x00,                              // child name_len = 9
        'l', 'o', 'o', 'p', '.', 'g', 'i', 'f', '.',  // child name
        0x00, 0x00,                              // child tag_count = 0
        0x00,                                    // child favorite = 0
        0x00,                                    // child sort_key = 0
        0x02,                                    // format = GIF (2)
        0x00, 0x00, 0x00, 0x00,                  // width = 0
        0x00, 0x00, 0x00, 0x00,                  // height = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // orig_size = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // created_ts = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // data_offset = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // data_length = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // thumb_offset = 0
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // thumb_length = 0
        0x00, 0x00,                              // saved_searches_count = 0
    };

    IndexNode out;
    REQUIRE(vault::deserialize_index(v6_blob, out));
    REQUIRE(out.children.size() == size_t{1});
    CHECK(!out.children[0].meta.animated);
}

TEST(index_rejects_out_of_range_animated_byte)
{
    IndexNode root = IndexNode::gallery("root");
    IndexNode gif  = IndexNode::image("loop.gif");
    gif.meta.format   = ImageFormat::GIF;
    gif.meta.animated = true;
    root.children.push_back(gif);

    std::vector<uint8_t> blob;
    vault::serialize_index(root, blob);
    blob.back() = 0x02;   // neither 0 nor 1

    IndexNode out;
    CHECK(!vault::deserialize_index(blob, out));
}

TEST(index_v8_accepts_insertion_sort_key)
{
    using namespace vault;
    IndexNode root = IndexNode::gallery("");
    IndexNode sub  = IndexNode::gallery("comics");
    sub.sort_key = SortKey::Insertion;
    root.children.push_back(std::move(sub));

    std::vector<uint8_t> blob;
    serialize_index(root, blob);
    CHECK_EQ(blob[0], INDEX_VERSION);
    CHECK_EQ(blob[0], 9);

    IndexNode out;
    CHECK(deserialize_index(blob, out));
    REQUIRE(out.children.size() == 1);
    CHECK(out.children[0].sort_key == SortKey::Insertion);
}

TEST(index_v8_rejects_out_of_range_sort_key)
{
    using namespace vault;
    IndexNode root = IndexNode::gallery("");
    std::vector<uint8_t> blob;
    serialize_index(root, blob);

    // Byte layout: [0]=version, [1]=type, [2..3]=name_len(0), [4..5]=tag_count(0),
    // [6]=favorite, [7]=sort_key.
    CHECK_EQ(blob[7], 0);
    blob[7] = 8;   // one past Insertion

    IndexNode out;
    CHECK(!deserialize_index(blob, out));
}

TEST(index_settings_round_trip)
{
    IndexNode root = IndexNode::gallery("");
    vault::VaultSettings s;
    s.default_sort    = vault::SortKey::NameAsc;
    s.tiles_show_tags = false;
    s.categories = {{"artist", 3}, {"parody", 7}};

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);

    IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(vault::deserialize_index(blob, out, searches, got));
    CHECK(got.default_sort == vault::SortKey::NameAsc);
    CHECK(!got.tiles_show_tags);
    REQUIRE(got.categories.size() == 2);
    CHECK(got.categories[0].name == "artist");
    CHECK_EQ(got.categories[0].swatch, 3);
    CHECK(got.categories[1].name == "parody");
    CHECK_EQ(got.categories[1].swatch, 7);
}

TEST(index_settings_empty_category_list_round_trips)
{
    // An empty list is a legitimate saved state and must NOT be re-seeded.
    IndexNode root = IndexNode::gallery("");
    vault::VaultSettings s;
    s.categories.clear();

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);

    IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(vault::deserialize_index(blob, out, searches, got));
    CHECK(got.categories.empty());
}

TEST(index_settings_dedupes_categories_case_insensitively)
{
    IndexNode root = IndexNode::gallery("");
    vault::VaultSettings s;
    s.categories = {{"Artist", 1}, {"artist", 9}, {"parody", 2}};

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);

    IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(vault::deserialize_index(blob, out, searches, got));
    REQUIRE(got.categories.size() == 2);
    CHECK(got.categories[0].name == "Artist");   // first casing wins
    CHECK_EQ(got.categories[0].swatch, 1);
    CHECK(got.categories[1].name == "parody");
}

TEST(index_settings_rejects_out_of_range_swatch)
{
    IndexNode root = IndexNode::gallery("");
    vault::VaultSettings s;
    s.categories = {{"artist", 3}};

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);
    blob.back() = vault::TAG_SWATCH_COUNT;   // swatch is the last byte written

    IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(!vault::deserialize_index(blob, out, searches, got));
}

TEST(index_settings_rejects_bad_tiles_flag)
{
    IndexNode root = IndexNode::gallery("");
    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, vault::VaultSettings{}, blob);

    // The settings block is the tail: default_sort, tiles_show_tags, cat_count(u16), desc_count(u16).
    // tiles_show_tags is at position -5 (2 bytes for desc_count after cat_count).
    const size_t tiles_at = blob.size() - 5;
    CHECK_EQ(blob[tiles_at], 1);
    blob[tiles_at] = 2;

    IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(!vault::deserialize_index(blob, out, searches, got));
}

TEST(index_settings_rejects_bad_default_sort)
{
    IndexNode root = IndexNode::gallery("");
    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, vault::VaultSettings{}, blob);

    // With the desc_count field added (Phase 51), default_sort is now at blob.size() - 6.
    blob[blob.size() - 6] = 9;   // default_sort byte, one past Insertion

    IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(!vault::deserialize_index(blob, out, searches, got));
}

TEST(index_settings_rejects_over_long_category_name)
{
    IndexNode root = IndexNode::gallery("");
    vault::VaultSettings s;
    s.categories = {{std::string(vault::INDEX_MAX_CATEGORY_BYTES + 1, 'a'), 0}};

    std::vector<uint8_t> blob;
    vault::serialize_index(root, {}, s, blob);   // writer clamps to the cap

    IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(vault::deserialize_index(blob, out, searches, got));
    REQUIRE(got.categories.size() == 1);
    CHECK_EQ(got.categories[0].name.size(), vault::INDEX_MAX_CATEGORY_BYTES);

    // A hand-forged blob declaring a longer name is rejected outright.
    std::vector<uint8_t> forged = blob;
    // With desc_count added at the end, the name_len is now 2 bytes earlier.
    const size_t name_len_at = forged.size() - 1 - 2 - vault::INDEX_MAX_CATEGORY_BYTES - 2;
    forged[name_len_at]     = 0xFF;
    forged[name_len_at + 1] = 0x00;
    IndexNode out2;
    std::vector<vault::SavedSearch> s2;
    vault::VaultSettings got2;
    CHECK(!vault::deserialize_index(forged, out2, s2, got2));
}

TEST(index_pre_v8_blob_reads_seeded_settings)
{
    // A v7 blob: same tree, version byte forced back to 7 and the settings
    // block stripped. Pre-v8 vaults must come back seeded, sorted Insertion.
    IndexNode root = IndexNode::gallery("");
    std::vector<uint8_t> v8;
    vault::serialize_index(root, {}, vault::VaultSettings{}, v8);

    // With desc_count added in v9, the settings block is now 6 bytes (was 4 in v8).
    // We strip the entire v9 settings block to create a v7 blob.
    std::vector<uint8_t> v7(v8.begin(), v8.end() - 6);   // drop the settings block
    v7[0] = 7;

    IndexNode out;
    std::vector<vault::SavedSearch> searches;
    vault::VaultSettings got;
    CHECK(vault::deserialize_index(v7, out, searches, got));
    CHECK(got.default_sort == vault::SortKey::Insertion);
    CHECK(got.tiles_show_tags);
    CHECK(got.categories == vault::VaultSettings::seeded().categories);
}

TEST(index_seeded_settings_are_eight_distinct_swatches)
{
    const auto s = vault::VaultSettings::seeded();
    REQUIRE(s.categories.size() == 8);
    CHECK(s.default_sort == vault::SortKey::Insertion);
    CHECK(s.tiles_show_tags);
    std::vector<uint8_t> swatches;
    for (const auto& c : s.categories) {
        CHECK(c.swatch < vault::TAG_SWATCH_COUNT);
        swatches.push_back(c.swatch);
    }
    std::ranges::sort(swatches);
    CHECK(std::ranges::adjacent_find(swatches) == swatches.end());   // all distinct
}
