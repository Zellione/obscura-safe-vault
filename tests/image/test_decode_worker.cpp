#include "test_framework.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

#include "crypto/secure_mem.h"
#include "image/decode_worker.h"
#include "fixtures.h"

namespace {

using namespace std::chrono_literals;

// Copy plain bytes into an mlock'd SecureBytes (the worker takes ownership).
crypto::SecureBytes to_secure(const std::vector<uint8_t>& v)
{
    crypto::SecureBytes sb;
    (void)sb.resize(v.size());
    std::copy(v.begin(), v.end(), sb.data());
    return sb;
}

// Spin (briefly) until a result is available; returns nullopt on timeout.
std::optional<image::DecodeWorker::Result> wait_result(image::DecodeWorker& w)
{
    for (int i = 0; i < 2000; ++i) {
        if (auto r = w.take_result()) return r;
        std::this_thread::sleep_for(1ms);
    }
    return std::nullopt;
}

} // namespace

TEST(decode_worker_decodes_submitted_image)
{
    image::DecodeWorker w;   // wake_event = 0: no SDL needed
    const auto png = fixtures::solid_png(8, 5, 10, 20, 30);
    w.submit(42, to_secure(png));

    auto r = wait_result(w);
    REQUIRE(r.has_value());
    CHECK_EQ(r->key, uint64_t{42});
    REQUIRE(r->image.has_value());
    CHECK_EQ(r->image->width, 8);
    CHECK_EQ(r->image->height, 5);
    // Result taken — no longer pending.
    CHECK_FALSE(w.pending(42));
}

TEST(decode_worker_reports_pending_until_taken)
{
    image::DecodeWorker w;
    w.submit(7, to_secure(fixtures::solid_png(4, 4, 0, 0, 0)));
    CHECK(w.pending(7));     // queued or decoding
    auto r = wait_result(w);
    REQUIRE(r.has_value());
    CHECK_FALSE(w.pending(7));
}

TEST(decode_worker_coalesces_duplicate_keys)
{
    image::DecodeWorker w;
    const auto png = fixtures::solid_png(4, 4, 1, 2, 3);
    w.submit(99, to_secure(png));
    w.submit(99, to_secure(png));   // duplicate key — coalesced, no second job

    auto r = wait_result(w);
    REQUIRE(r.has_value());
    CHECK_EQ(r->key, uint64_t{99});
    // Only one result was ever produced for key 99.
    CHECK_FALSE(w.take_result().has_value());
}

TEST(decode_worker_empty_image_on_garbage)
{
    image::DecodeWorker w;
    w.submit(5, to_secure(fixtures::malformed_jpeg()));
    auto r = wait_result(w);
    REQUIRE(r.has_value());
    CHECK_EQ(r->key, uint64_t{5});
    CHECK_FALSE(r->image.has_value());   // decode failed -> no image
}

TEST(decode_worker_fetch_success_decodes)
{
    image::DecodeWorker w(0);
    const auto png = fixtures::solid_png(8, 5, 10, 20, 30);
    w.submit_fetch(7, [&png](crypto::SecureBytes& out) {
        out = crypto::SecureBytes(png.size());
        std::copy(png.begin(), png.end(), out.data());
        return true;
    });
    auto r = wait_result(w);
    REQUIRE(r.has_value());
    CHECK_EQ(r->key, uint64_t{7});
    REQUIRE(r->image.has_value());
    CHECK_EQ(r->image->width, 8);
    CHECK_EQ(r->image->height, 5);
}

TEST(decode_worker_fetch_failure_reports_empty)
{
    image::DecodeWorker w(0);
    w.submit_fetch(9, [](crypto::SecureBytes&) { return false; });
    auto r = wait_result(w);
    REQUIRE(r.has_value());
    CHECK_EQ(r->key, uint64_t{9});
    CHECK_FALSE(r->image.has_value());
}

TEST(decode_worker_fetch_coalesces_by_key)
{
    image::DecodeWorker w(0);
    w.submit_fetch(3, [](crypto::SecureBytes&) { return false; });
    w.submit_fetch(3, [](crypto::SecureBytes&) { return false; });   // no-op
    CHECK(w.pending(3));
    auto r = wait_result(w);
    REQUIRE(r.has_value());
    CHECK_EQ(r->key, uint64_t{3});
    // Only one result for key 3
    CHECK_FALSE(w.take_result().has_value());
}
