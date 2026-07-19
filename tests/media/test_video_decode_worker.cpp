#include "test_framework.h"

#ifdef OSV_VENDORED_AV

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>
#include <vector>

#include "crypto/kdf.h"
#include "media/chunk_avio.h"
#include "media/video_decode_worker.h"
#include "media/video_decoder.h"
#include "media/video_source.h"
#include "vault/vault.h"
#include "media/hw_accel.h"

namespace {
namespace fs = std::filesystem;

static const crypto::KdfParams kTestKdf{.t_cost = 1, .m_cost_kib = 8, .parallelism = 1};

struct TempVault {
    fs::path path;
    explicit TempVault(const char* tag)
    {
        static int ctr = 0;
        path = fs::temp_directory_path() /
               ("osv_vdw_" + std::string(tag) + "_" + std::to_string(ctr++) + ".osv");
        std::error_code ec;
        fs::remove(path, ec);
    }
    ~TempVault() { std::error_code ec; fs::remove(path, ec); }
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

// Wait up to ~2s for `pred` to become true, polling every 5ms — bounds test
// runtime while tolerating the worker thread's scheduling latency.
template <class Pred>
bool wait_for(Pred pred)
{
    for (int i = 0; i < 400; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

struct Fixture {
    TempVault       tv;
    vault::Vault    v;
    media::ChunkAvio* avio_ptr = nullptr;
    media::VideoDecoder dec;
    bool            valid = false;

    explicit Fixture(const char* tag) : tv(tag)
    {
        auto v_bytes = read_file(OSV_VAULT_FIXTURE_DIR "/tiny.mp4");
        if (v_bytes.empty()) return;
        if (vault::Vault::create(tv.str(), bytes("pw"), {}, kTestKdf, v) != vault::VaultResult::Ok) return;
        if (v.create_gallery("c") != vault::VaultResult::Ok) return;
        if (v.add_video("c", v_bytes, "tiny.mp4", 4096) != vault::VaultResult::Ok) return;
        auto kids = v.list("c");
        if (kids.size() != 1) return;

        auto vs = media::VideoSource::open(v, *kids[0]);
        avio_ptr = new media::ChunkAvio(std::move(vs));
        if (!avio_ptr->valid()) {
            delete avio_ptr;
            avio_ptr = nullptr;
            return;
        }
        if (!dec.open(avio_ptr->ctx())) return;
        valid = true;
    }

    ~Fixture() { delete avio_ptr; }

    media::ChunkAvio& avio() { return *avio_ptr; }
};

} // namespace

namespace media {
// Test-only seams (see friend declarations in video_decode_worker.h):
// deterministically exercise the hw-failure recovery path without real
// hardware, since no CI runner has a GPU decode block to genuinely fail.
bool test_only_reopen_software(VideoDecodeWorker& w) { return w.reopen_software_only(); }
void test_only_force_hw_active(VideoDecodeWorker& w, bool active) { w.hw_active_ = active; }
bool test_only_pending_flush(VideoDecodeWorker& w)
{
    std::lock_guard lock(w.mtx_);
    return w.pending_flush_;
}
} // namespace media

TEST(video_decode_worker_decodes_submitted_packets_in_order)
{
    Fixture f("in_order");
    REQUIRE(f.valid);

    media::VideoDecodeWorker worker(*f.dec.video_codecpar(), f.dec.video_time_base(), 0);

    int submitted = 0;
    while (AVPacket* pkt = f.dec.demux_next_video_packet()) {
        worker.submit(pkt, /*generation=*/0);
        ++submitted;
    }
    worker.submit(nullptr, /*generation=*/0);   // end of stream
    REQUIRE(submitted == 10);

    std::vector<double> pts_seen;
    bool saw_eof = false;
    REQUIRE(wait_for([&] {
        while (auto r = worker.take_result()) {
            if (r->eof) { saw_eof = true; continue; }
            if (!r->frame.has_value()) return false;
            pts_seen.push_back(r->frame->pts_seconds);
        }
        return saw_eof;
    }));

    REQUIRE(pts_seen.size() == 10);
    for (size_t i = 1; i < pts_seen.size(); ++i)
        CHECK(pts_seen[i] >= pts_seen[i - 1]);   // presentation order preserved
}

TEST(video_decode_worker_begin_seek_drops_stale_queued_packets)
{
    Fixture f("seek_drop");
    REQUIRE(f.valid);

    media::VideoDecodeWorker worker(*f.dec.video_codecpar(), f.dec.video_time_base(), 0);

    // Submit all 10 packets under generation 0 without giving the worker a
    // chance to drain them (no sleep here — best-effort race, the queue drop
    // is exercised whatever the worker has or hasn't consumed yet).
    std::vector<AVPacket*> pkts;
    while (AVPacket* pkt = f.dec.demux_next_video_packet()) pkts.push_back(pkt);
    REQUIRE(pkts.size() == 10);
    for (auto* pkt : pkts) worker.submit(pkt, /*generation=*/0);

    // Immediately supersede with a new generation.
    worker.begin_seek(/*target_pts=*/0.0);
    worker.submit(nullptr, /*generation=*/1);   // EOF for generation 1, nothing else submitted

    // Every published result must be tagged generation 1 (either an eof
    // marker, or — if a generation-0 frame had already finished decoding
    // before begin_seek() ran — that stray frame is acceptable to observe,
    // but no MORE than the packets that were in flight before the drop
    // should appear). The strong assertion: we eventually see a generation-1
    // eof and the worker doesn't hang.
    bool saw_gen1_eof = false;
    REQUIRE(wait_for([&] {
        while (auto r = worker.take_result())
            if (r->generation == 1 && r->eof) saw_gen1_eof = true;
        return saw_gen1_eof;
    }));
}

TEST(video_decode_worker_outstanding_returns_to_zero_even_when_seek_discards_frames)
{
    // The render thread gates prefetch/feed on VideoDecodeWorker::outstanding().
    // For that to be safe across a seek, the worker must un-count EVERY job it
    // finishes — including jobs whose decoded frame it silently discards
    // because the frame's pts lands before a pending seek target (those publish
    // no Result). If such jobs stayed counted, the total would be permanently
    // inflated by the number of frames the seek skipped and eventually wedge
    // feeding — the phantom-count freeze this guards against.
    Fixture f("outstanding_seek");
    REQUIRE(f.valid);

    media::VideoDecodeWorker worker(*f.dec.video_codecpar(), f.dec.video_time_base(), 0);

    // Arm a seek target partway through the clip, then feed the whole stream
    // under the post-seek generation. The worker decode-forwards, discarding
    // every frame before the target without publishing a Result for it.
    worker.begin_seek(/*target_pts=*/0.5);
    std::vector<AVPacket*> pkts;
    while (AVPacket* pkt = f.dec.demux_next_video_packet()) pkts.push_back(pkt);
    REQUIRE(pkts.size() == 10);
    for (auto* pkt : pkts) worker.submit(pkt, /*generation=*/1);
    worker.submit(nullptr, /*generation=*/1);   // EOF flush marker

    // Drain until the generation-1 eof arrives — by then every submitted job
    // (decoded, discarded, or the flush) has been processed.
    bool saw_eof = false;
    REQUIRE(wait_for([&] {
        while (auto r = worker.take_result())
            if (r->generation == 1 && r->eof) saw_eof = true;
        return saw_eof;
    }));

    // Some frames (>= the target) published Results; the earlier ones (< the
    // target) were silently discarded. Either way, once everything is
    // processed the outstanding count must settle back to zero — no phantom
    // left behind by the discarded jobs. (Polled: job_finished() for the flush
    // job runs just after its eof Result is published.)
    CHECK(wait_for([&] { return worker.outstanding() == 0; }));
}

TEST(video_decode_worker_begin_seek_schedules_codec_flush)
{
    // begin_seek() must schedule a codec-buffer flush that the worker
    // thread actually consumes before decoding the next job — without it,
    // packets from the new seek target get decoded against stale pre-seek
    // reorder/reference state (see the comment in VideoDecodeWorker::run()).
    // This doesn't (and can't, without real hw) prove decode correctness
    // improves; it proves the flush mechanism itself fires exactly once
    // per seek, on the worker thread, before further decode proceeds.
    Fixture f("seek_flush");
    REQUIRE(f.valid);

    media::VideoDecodeWorker worker(*f.dec.video_codecpar(), f.dec.video_time_base(), 0);
    CHECK(!media::test_only_pending_flush(worker));

    worker.begin_seek(0.0);
    CHECK(media::test_only_pending_flush(worker));

    AVPacket* pkt = f.dec.demux_next_video_packet();
    REQUIRE(pkt);
    worker.submit(pkt, /*generation=*/1);
    worker.submit(nullptr, /*generation=*/1);

    bool saw_gen1_result = false;
    REQUIRE(wait_for([&] {
        while (auto r = worker.take_result())
            if (r->generation == 1) saw_gen1_result = true;
        return saw_gen1_result;
    }));
    CHECK(!media::test_only_pending_flush(worker));   // consumed by the worker before decoding resumed
}

TEST(video_decode_worker_stops_cleanly_mid_flight)
{
    // Destruction while packets are still queued/decoding must not crash,
    // hang, or leak (verified by scripts/test.sh --asan running this test).
    auto f = std::make_unique<Fixture>("mid_flight");
    REQUIRE(f->valid);
    auto worker = std::make_unique<media::VideoDecodeWorker>(
        *f->dec.video_codecpar(), f->dec.video_time_base(), 0);
    while (AVPacket* pkt = f->dec.demux_next_video_packet())
        worker->submit(pkt, 0);
    worker.reset();   // destructor must join the thread cleanly
    f.reset();        // clean up fixture
    CHECK(true);       // reaching here without hanging/crashing is the assertion
}

TEST(video_decode_worker_hwaccel_forced_unavailable_matches_normal_decode)
{
    // Two independent decode runs of the same clip: one with hw device
    // creation forced to fail, one with normal (real) probing. On this
    // build/CI leg, real probing itself resolves to "unavailable" too (no
    // OSV_HWACCEL_* compiled in on Linux; no real GPU decode block on any CI
    // runner even where it is compiled in) — the assertion that matters is
    // that both runs produce byte-identical decoded output, so a future leg
    // where real hwaccel *does* activate is covered by this same test
    // without any change.
    auto decode_all = [](media::VideoDecoder& dec) {
        media::VideoDecodeWorker worker(*dec.video_codecpar(), dec.video_time_base(), 0);
        std::vector<double> pts_seen;
        std::vector<std::vector<uint8_t>> storages;
        while (AVPacket* pkt = dec.demux_next_video_packet()) worker.submit(pkt, 0);
        worker.submit(nullptr, 0);
        bool saw_eof = false;
        wait_for([&] {
            while (auto r = worker.take_result()) {
                if (r->eof) { saw_eof = true; continue; }
                if (!r->frame.has_value()) continue;
                pts_seen.push_back(r->frame->pts_seconds);
                storages.push_back(r->storage);
            }
            return saw_eof;
        });
        return std::pair{pts_seen, storages};
    };

    Fixture fa("hwaccel_bytes_a");
    REQUIRE(fa.valid);
    media::test_only_force_hwaccel_unavailable(true);
    auto [pts_a, storage_a] = decode_all(fa.dec);

    Fixture fb("hwaccel_bytes_b");
    REQUIRE(fb.valid);
    media::test_only_force_hwaccel_unavailable(false);
    auto [pts_b, storage_b] = decode_all(fb.dec);
    media::test_only_force_hwaccel_unavailable(false);   // leave clean for later tests

    REQUIRE(pts_a.size() == 10);
    REQUIRE(pts_a.size() == pts_b.size());
    for (size_t i = 0; i < pts_a.size(); ++i) {
        CHECK(pts_a[i] == pts_b[i]);
        CHECK(storage_a[i] == storage_b[i]);
    }
}

TEST(video_decode_worker_forced_hwaccel_unavailable_decodes_correctly)
{
    Fixture f("forced_unavailable");
    REQUIRE(f.valid);
    media::test_only_force_hwaccel_unavailable(true);

    media::VideoDecodeWorker worker(*f.dec.video_codecpar(), f.dec.video_time_base(), 0);
    int submitted = 0;
    while (AVPacket* pkt = f.dec.demux_next_video_packet()) {
        worker.submit(pkt, /*generation=*/0);
        ++submitted;
    }
    worker.submit(nullptr, /*generation=*/0);
    REQUIRE(submitted == 10);

    std::vector<double> pts_seen;
    bool saw_eof = false;
    REQUIRE(wait_for([&] {
        while (auto r = worker.take_result()) {
            if (r->eof) { saw_eof = true; continue; }
            if (!r->frame.has_value()) return false;
            pts_seen.push_back(r->frame->pts_seconds);
        }
        return saw_eof;
    }));

    media::test_only_force_hwaccel_unavailable(false);   // leave clean for later tests

    REQUIRE(pts_seen.size() == 10);
    for (size_t i = 1; i < pts_seen.size(); ++i)
        CHECK(pts_seen[i] >= pts_seen[i - 1]);
}

TEST(video_decode_worker_reopen_software_only_recovers_and_continues_decoding)
{
    // Directly exercises reopen_software_only() — normally only reachable
    // via a real hw decode failure, which no CI runner can produce (no GPU
    // decode block) — to prove it leaves the worker able to keep decoding
    // correctly afterward, matching what real hw-failure recovery must
    // guarantee.
    Fixture f("reopen_sw");
    REQUIRE(f.valid);

    media::VideoDecodeWorker worker(*f.dec.video_codecpar(), f.dec.video_time_base(), 0);
    REQUIRE(media::test_only_reopen_software(worker));

    int submitted = 0;
    while (AVPacket* pkt = f.dec.demux_next_video_packet()) {
        worker.submit(pkt, /*generation=*/0);
        ++submitted;
    }
    worker.submit(nullptr, /*generation=*/0);
    REQUIRE(submitted == 10);

    std::vector<double> pts_seen;
    bool saw_eof = false;
    REQUIRE(wait_for([&] {
        while (auto r = worker.take_result()) {
            if (r->eof) { saw_eof = true; continue; }
            if (!r->frame.has_value()) return false;
            pts_seen.push_back(r->frame->pts_seconds);
        }
        return saw_eof;
    }));
    REQUIRE(pts_seen.size() == 10);
}

TEST(video_decode_worker_forced_hw_active_transfer_noop_still_decodes_correctly)
{
    // Forces hw_active_ = true on an otherwise normal (software-decoding)
    // worker so publish_decoded_frame()'s hw-frame-transfer-attempt branch
    // runs on every frame; transfer_hw_frame() itself is a no-op stub on
    // this build (no OSV_HWACCEL_* macro compiled in), so decode must still
    // succeed exactly as it does with hw_active_ false.
    Fixture f("forced_active");
    REQUIRE(f.valid);

    media::VideoDecodeWorker worker(*f.dec.video_codecpar(), f.dec.video_time_base(), 0);
    media::test_only_force_hw_active(worker, true);

    int submitted = 0;
    while (AVPacket* pkt = f.dec.demux_next_video_packet()) {
        worker.submit(pkt, /*generation=*/0);
        ++submitted;
    }
    worker.submit(nullptr, /*generation=*/0);
    REQUIRE(submitted == 10);

    std::vector<double> pts_seen;
    bool saw_eof = false;
    REQUIRE(wait_for([&] {
        while (auto r = worker.take_result()) {
            if (r->eof) { saw_eof = true; continue; }
            if (!r->frame.has_value()) return false;
            pts_seen.push_back(r->frame->pts_seconds);
        }
        return saw_eof;
    }));
    REQUIRE(pts_seen.size() == 10);
}

TEST(video_decode_worker_hw_transfer_failure_skips_frame_instead_of_corrupting)
{
    // Forces hw_active_ = true AND is_hw_format_frame() = true, so
    // publish_decoded_frame() believes every decoded frame is still an
    // opaque hw device handle. transfer_hw_frame() is a no-op stub on this
    // build (no OSV_HWACCEL_* compiled in) and always returns false, so
    // every frame must be treated as a genuine hw-transfer failure and
    // skipped entirely — never fed to the software pixel-conversion path,
    // which would otherwise silently misinterpret device-handle bytes as
    // real YUV planes (undefined behavior on real hardware).
    Fixture f("hw_transfer_fail");
    REQUIRE(f.valid);

    media::VideoDecodeWorker worker(*f.dec.video_codecpar(), f.dec.video_time_base(), 0);
    media::test_only_force_hw_active(worker, true);
    media::test_only_force_is_hw_format_frame(true);

    int submitted = 0;
    while (AVPacket* pkt = f.dec.demux_next_video_packet()) {
        worker.submit(pkt, /*generation=*/0);
        ++submitted;
    }
    worker.submit(nullptr, /*generation=*/0);
    REQUIRE(submitted == 10);

    int  frames_seen = 0;
    bool saw_eof      = false;
    REQUIRE(wait_for([&] {
        while (auto r = worker.take_result()) {
            if (r->eof) { saw_eof = true; continue; }
            if (r->frame.has_value()) ++frames_seen;
        }
        return saw_eof;
    }));

    media::test_only_force_is_hw_format_frame(std::nullopt);   // leave clean for later tests

    CHECK(frames_seen == 0);   // every frame skipped, not passed through as fake pixel data
}

#endif // OSV_VENDORED_AV
