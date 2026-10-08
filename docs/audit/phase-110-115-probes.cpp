// Audit reproductions for d3dd346; expected to fail until findings are fixed.
// Not part of the regular test suite. Build/run commands are in AUDIT.md.
// Uses synthetic data only; retains generated /tmp/osv-audit-* fixtures.
#include "test_framework.h"
#include "vault/vault.h"
#include "vault/staging.h"
#include "vault/legacy_converter.h"
#include "vault/v3_read_session.h"
#include "ui/tile_thumb.h"
#include "ui/dup_scan.h"
#include "vault/commit_lane.h"
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <cassert>

namespace {
constexpr std::array<uint8_t,2> password{'p','w'};
constexpr crypto::KdfParams kdf{1,8,1};
struct Fixture {
    std::filesystem::path path;
    vault::Vault source;
    Fixture() {
        char pattern[] = "/tmp/osv-audit-XXXXXX";
        path = ::mkdtemp(pattern);
        assert(vault::Vault::create((path/"source.osv").string(), password, {}, kdf, source) == vault::VaultResult::Ok);
    }
    void image(const char* name) {
        constexpr std::array<uint8_t,4> bytes{1,2,3,4};
        assert(source.add_image("",bytes,name) == vault::VaultResult::Ok);
    }
    vault::LegacyConversionReport convert() {
        source.lock();
        assert(source.unlock(password,{}) == vault::VaultResult::Ok);
        return vault::convert_legacy_vault(source,{path/"dest.osv",password,{},kdf});
    }
};
}
TEST(audit_tag_order_conversion) {
    Fixture f; f.image("one");
    REQUIRE(f.source.set_tags("one",{"zebra","alpha"}) == vault::VaultResult::Ok);
    auto r=f.convert();
    std::printf("AUDIT tag_order status=%d deep=%d cold=%d logical=%d\n",int(r.status),r.deep_verified,r.cold_reopened,r.logical_verified);
    CHECK(r.status == vault::LegacyConversionStatus::Ok);
}
TEST(audit_tag_case_conversion) {
    Fixture f; f.image("one"); f.image("two");
    REQUIRE(f.source.set_tags("one",{"Blue"}) == vault::VaultResult::Ok);
    REQUIRE(f.source.set_tags("two",{"blue"}) == vault::VaultResult::Ok);
    auto r=f.convert();
    std::printf("AUDIT tag_case status=%d\n",int(r.status));
    CHECK(r.status == vault::LegacyConversionStatus::Ok);
}
TEST(audit_video_duration_conversion) {
    Fixture f;
    constexpr std::array<uint8_t,4> bytes{1,2,3,4};
    vault::StagedVideoInfo meta;
    meta.container=vault::VideoContainer::MP4;
    meta.duration_us=1'234'567;
    REQUIRE(vault::add_video_prestaged(f.source,"",bytes,"video.mp4",meta,0) == vault::VaultResult::Ok);
    auto r=f.convert();
    std::printf("AUDIT duration status=%d\n",int(r.status));
    CHECK(r.status == vault::LegacyConversionStatus::Ok);
}
TEST(audit_partial_conversion_opens_normally) {
    Fixture f; f.image("one");
    std::atomic_bool cancel{true};
    auto r=vault::convert_legacy_vault(f.source,{f.path/"dest.osv",password,{},kdf,&cancel});
    REQUIRE(r.status == vault::LegacyConversionStatus::Cancelled);
    vault::Vault partial;
    REQUIRE(vault::Vault::open((f.path/"dest.osv").string(),partial)==vault::VaultResult::Ok);
    auto opened=partial.unlock(password,{});
    std::printf("AUDIT partial unlock=%d readonly=%d\n",int(opened),vault::vault_is_read_only(partial));
    CHECK(opened != vault::VaultResult::Ok);
}
TEST(audit_stale_header_after_password_change) {
    Fixture f;
    vault::Vault first, stale;
    auto path=(f.path/"new.osv").string();
    REQUIRE(vault::Vault::create_directory(path,password,{},kdf,first)==vault::VaultResult::Ok);
    REQUIRE(vault::Vault::open(path,stale)==vault::VaultResult::Ok);
    constexpr std::array<uint8_t,3> replacement{'n','e','w'};
    REQUIRE(first.change_password(password,{},replacement,{})==vault::VaultResult::Ok);
    first.lock();
    auto result=stale.unlock(password,{});
    std::printf("AUDIT stale_header old_password_unlock=%d\n",int(result));
    CHECK(result==vault::VaultResult::AuthFailed);
}
TEST(audit_import_cache_identity) {
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path/"new.osv").string(),password,{},kdf,v)==vault::VaultResult::Ok);
    constexpr std::array<uint8_t,4> first{1,2,3,4},second{5,6,7,8};
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(first));
    REQUIRE(vault::add_image_prestaged(v,"",first,"one",thumb,0)==vault::VaultResult::Ok);
    REQUIRE(vault::add_image_prestaged(v,"",second,"two",thumb,0)==vault::VaultResult::Ok);
    auto one=ui::thumb_key_for(*v.resolve_node("one"));
    auto two=ui::thumb_key_for(*v.resolve_node("two"));
    std::printf("AUDIT imported keys=%llu,%llu\n",(unsigned long long)one.key,(unsigned long long)two.key);
    CHECK(one.key!=two.key);
}
TEST(audit_original_span_reads_thumbnail) {
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path/"new.osv").string(),password,{},kdf,v)==vault::VaultResult::Ok);
    constexpr std::array<uint8_t,4> original{1,2,3,4},thumbbytes{5,6,7,8};
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(thumbbytes));
    REQUIRE(vault::add_image_prestaged(v,"",original,"one",thumb,0)==vault::VaultResult::Ok);
    crypto::SecureBytes result;
    REQUIRE(vault::read_thumb_span(v,vault::image_data_chunk_ref(*v.resolve_node("one")),result)==vault::VaultResult::Ok);
    std::printf("AUDIT original span first byte=%d expected=1\n",int(result.data()[0]));
    CHECK(std::ranges::equal(result.as_span(),original));
}
TEST(audit_false_exact_duplicates) {
    Fixture f;
    vault::Vault v;
    REQUIRE(vault::Vault::create_directory((f.path/"new.osv").string(),password,{},kdf,v)==vault::VaultResult::Ok);
    constexpr std::array<uint8_t,4> first{1,2,3,4},second{5,6,7,8};
    vault::StagedThumb thumb;
    REQUIRE(thumb.thumb_jpeg.assign(first));
    REQUIRE(vault::add_image_prestaged(v,"",first,"one",thumb,0)==vault::VaultResult::Ok);
    REQUIRE(vault::add_image_prestaged(v,"",second,"two",thumb,0)==vault::VaultResult::Ok);
    ui::DupScanJob job;
    job.start(v,ui::collect_scan_items(v),false);
    while(job.active()) std::this_thread::yield();
    auto result=job.take_outcome();
    REQUIRE(result.has_value());
    std::printf("AUDIT false exact groups=%zu skipped=%zu\n",result->groups.size(),result->skipped);
    CHECK(result->groups.empty());
}
TEST(audit_directory_import_commit_lane) {
    const pid_t child=fork();
    REQUIRE(child>=0);
    if(child==0) {
        const rlimit limit{0,0}; (void)setrlimit(RLIMIT_CORE,&limit);
        Fixture f;
        vault::Vault v;
        assert(vault::Vault::create_directory((f.path/"new.osv").string(),password,{},kdf,v)==vault::VaultResult::Ok);
        vault::CommitLane lane;
        lane.start(v);
        const bool enqueued=lane.enqueue_snapshot();
        const bool flushed=lane.flush();
        lane.stop();
        _exit(enqueued && flushed ? 0 : 1);
    }
    int status=0;
    REQUIRE(waitpid(child,&status,0)==child);
    std::printf("AUDIT directory commit child signal=%d exit=%d\n",WIFSIGNALED(status)?WTERMSIG(status):0,WIFEXITED(status)?WEXITSTATUS(status):-1);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status)==0);
}
TEST(audit_case_distinct_siblings) {
    Fixture f;
    REQUIRE(f.source.create_gallery("Trip")==vault::VaultResult::Ok);
    REQUIRE(f.source.create_gallery("trip")==vault::VaultResult::Ok);
    auto r=f.convert();
    std::printf("AUDIT case siblings conversion=%d\n",int(r.status));
    CHECK(r.status==vault::LegacyConversionStatus::Ok);
}
TEST(audit_verify_media_without_original) {
    Fixture f;
    auto created=vault::v3::ReadSession::create(f.path/"new.osv",password,{},kdf);
    REQUIRE(created.session);
    auto root=created.session->root();
    auto image=vault::IndexNode::image("missing.jpg");
    image.node_id=root.node_id;
    image.node_id[0]^=0x80;
    image.meta.format=vault::ImageFormat::JPEG;
    image.meta.orig_size=4;
    root.children.push_back(std::move(image));
    REQUIRE(created.session->commit_metadata(root,created.session->settings(),{})==vault::v3::ReadStatus::Ok);
    auto report=created.session->verify(vault::v3::VerifyDepth::Deep);
    std::printf("AUDIT missing original verify=%d checked=%llu findings=%zu\n",int(report.status),(unsigned long long)report.objects_checked,report.findings.size());
    CHECK(report.status!=vault::v3::RecoveryStatus::Ok);
}
