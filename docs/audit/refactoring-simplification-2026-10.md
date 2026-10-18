# Code refactoring and simplification audit

The best opportunities are to make vault lifetime, commit behavior, and worker
ownership explicit, then extract the import and gallery responsibilities that
currently depend on those contracts. Small shared helpers can remove repetition,
but splitting large files alone will not reduce the number of invariants a caller
must remember.

This audit covers the C++ application, tests, build configuration, and current
architecture documentation at commit `0116fd6`, on
`phase-120-gallery-folder-export`, reviewed on 2026-10-18. Findings record the
original proposals; implementation progress is tracked below. They are grounded
in source inspection; failure
scenarios identified below need the specified regression tests before being
treated as reproduced defects. No runtime benchmarks or test suites were run for
this documentation-only audit.

## Implementation progress

[Refactoring step 1](../roadmap/refactoring-01-vault-fixtures.md) begins R14 with
explicit checked backend fixtures, shared bounded queue waits, and targeted
cold-reopen import/transfer regressions. Local Debug, Release, ASAN, and no-AV
checks pass; its PR tracks CI and SonarCloud validation.
The findings below retain the original audit baseline; the remaining items have
not been implemented.

## Scope and baseline

The inventory excludes vendored dependencies, generated build outputs, assets,
and experimental tools. Counts include physical lines, comments, and blank lines
in `.cpp`, `.h`, and `.c` files; the scripts row also includes `.sh` files.

| Area | Files | Lines |
| --- | ---: | ---: |
| `src/` | 456 | 68,937 |
| `tests/` | 265 | 54,494 |
| `scripts/` | 12 | 1,116 |

| Largest implementation files | Lines | Responsibilities worth separating |
| --- | ---: | --- |
| `src/vault/vault.cpp` | 2,882 | Backend dispatch, lifetime, tree mutations, search, migration, maintenance |
| `src/ui/gallery_grid.cpp` | 2,542 | Browse state, import prompts, operations, modal dispatch, rendering |
| `src/vault/v3_db.cpp` | 1,809 | Connection setup, schema migration, metadata synchronization, repositories, conversion bookkeeping |
| `src/app/app.cpp` | 1,563 | Navigation, session ownership, overlays, maintenance, event loop |
| `src/ui/import_queue.cpp` | 1,476 | Task scheduling, decode pool, staging, attachment, tag review, commit policy |

These counts locate review effort; they are not complexity scores. The detailed
review follows representative lifecycle, import, persistence, query, and media
paths through these files and their collaborators. It is not a line-by-line
security audit of every source file. The prior
[vault leakage and corruption audit](vault-leakage-corruption-2026-10.md) remains
relevant, particularly its fail-closed snapshot and staged-object protections.

## Priority and delivery order

Priority measures maintenance value and the importance of making a contract
explicit, not vulnerability severity. Risk means regression risk while changing
the code. Effort is relative: S is a narrow extraction, M spans a subsystem, and L
crosses ownership or persistence boundaries. These are estimates, not schedules.

| ID | Candidate | Priority | Effort | Risk | Prerequisite |
| --- | --- | --- | --- | --- | --- |
| R01 | Own active-vault teardown in one component | High | M | High | Lifecycle characterization tests |
| R02 | Separate synchronous v3 commits from legacy queued commits | High | M | High | R01 and explicit commit contracts |
| R03 | Make mutation failure behavior explicit and consistent | High | L | High | Failure injection and R02 contracts |
| R04 | Use one SQL transaction lifetime helper | High | M | High | Existing database rollback tests |
| R05 | Move shared query and ordering models out of UI ownership | Medium | M | Medium | Serialization compatibility tests |
| R06 | Share media preparation before backend storage | Medium | M | Medium | R14 backend fixtures |
| R07 | Type import records and share their application path | High | M | High | R14, then R02 for commit changes |
| R08 | Extract import interaction from GalleryGrid | Medium | M | Medium | R07 record contract |
| R09 | Share the small background-job completion protocol | Medium | M | High | Ownership and cancellation tests |
| R10 | Separate storage references from thumbnail cache identity | Medium | L | High | R14 backend fixtures |
| R11 | Consolidate FFmpeg packet cleanup and padded scratch layout | Medium | M | High | Media ownership and canary tests |
| R12 | Share only preference read mechanics | Low | S | Low | Existing preference tests |
| R13 | Name reusable build source groups | Medium | M | Medium | Build matrix baseline |
| R14 | Make test backend selection explicit | High | M | Low | None; start here |
| R15 | Correct current-state comments and memory drift | Medium | S | Low | Check each statement against source |

Start with R14 and R15. R05 is a useful first code refactor because its intended
behavior can remain byte-for-byte compatible. Then tackle R01, R02, and R04 in
separate PRs. R03 is a behavior contract change and should follow those smaller
steps. R10 is a later cleanup, not a prerequisite for all other work.

## Detailed findings

### R01 Give active-vault teardown one owner

**Evidence.** [App::promote_pending](../../src/app/app.cpp#L174), the `ToUnlock`
and `LockActive` branches of [App::apply_nav](../../src/app/app.cpp#L1110),
[App::maybe_auto_lock](../../src/app/app.cpp#L1232), and
[App::shutdown](../../src/app/app.cpp#L1534) each contain parts of the same
sequence: stop screen work, finish or cancel imports, manage the commit lane,
lock the vault, clear session state, and handle the warm second vault.
[CommitLane::stop](../../src/vault/commit_lane.cpp#L99) also detaches the vault's
borrowed router pointer. This is already guarded by dedicated regressions; it
should not be described as an unfixed dangling-pointer defect.

**Change.** Introduce an internal session owner with explicit close policies for
manual lock, idle lock, switching, and shutdown. Move the vault, lane, and queue
binding lifecycle into that owner incrementally. Preserve the actual current
timing of unlock-screen transitions. Keep screen navigation and warm-vault
policy above it; an idle lock and a manual lock intentionally have different
effects on a KeepSession second vault.

**Benefit.** A new teardown path would select a policy rather than reproduce
the required ordering. Grouping fields into another passive struct without
moving the lifecycle operations would not achieve this.

**Tests and acceptance.** Characterize manual lock, idle lock, quit during
migration, canceled switching, and warm-vault promotion. Assert that borrowed
vault work has joined before key wipe, imports finish their permitted partial
work, and no router remains attached to a destroyed lane. Extend
[commit-lane tests](../../tests/vault/test_commit_lane.cpp),
[dual-gallery ownership tests](../../tests/ui/test_dual_gallery.cpp), and
[warm transfer tests](../../tests/ui/test_transfer_dialog.cpp). All active-vault
teardown sites should delegate the shared ordering to one implementation.

### R02 Make commit semantics visible in the interface

**Evidence.** [Vault::commit_index](../../src/vault/vault.cpp#L2857) immediately
dispatches v3 metadata to `ReadSession::commit_metadata`; the legacy path may
enqueue a snapshot. [CommitLane::enqueue_snapshot](../../src/vault/commit_lane.cpp#L23)
also special-cases v3 and performs its transaction on the calling thread.
`CommitLane::start` nevertheless launches a worker, and `running()` describes
whether that thread is joinable. Import callers use the same enqueue/flush
vocabulary for both behaviors.

**Change.** Separate the commit coordinator's contract from its legacy worker
implementation. Represent whether success means durable completion or acceptance
of pending work. Use a synchronous v3 implementation initially and preserve the
legacy snapshot/coalescing implementation for the paths that need it. Do not
introduce background SQL connections as part of this extraction.

**Benefit.** Thread ownership, flush requirements, and error handling become
reviewable at call sites. The v3 implementation no longer needs a parked legacy
writer thread merely to satisfy `running()`.

**Tests and acceptance.** Retain stopped-lane rejection, failure latching,
coalescing, and shutdown tests. Preserve
`import_queue_directory_durability_and_empty_shutdown` in
[test_import_queue.cpp](../../tests/ui/test_import_queue.cpp#L83). Check both
successful cold reopen and failure propagation through the queue.

**Separate performance work.** `Database::sync_metadata` traverses the full tree
and rebuilds metadata joins. A small interactive edit can therefore perform
substantial synchronous work. Measure this through the existing
[facade benchmark](../../tests/vault/test_v3_bench.cpp#L177) before proposing
incremental mutation or asynchronous snapshots. Neither a speedup nor an
acceptable UI latency has been established by this audit.

### R03 Specify what survives a failed mutation

**Evidence.** [rename_tag](../../src/vault/vault.cpp#L2188) plans changes and
restores swapped tags and saved searches when `commit_index()` reports failure.
[rename_node](../../src/vault/vault.cpp#L2070) assigns the name before committing;
[set_vault_settings](../../src/vault/vault.cpp#L2061) replaces settings before
committing. Batch removal explicitly documents that the tree is already
mutated on failure in [vault.h](../../src/vault/vault.h#L600). These are different
contracts, not merely different coding styles. Legacy enqueue success also does
not establish that a later background write succeeded.

**Change.** First document the existing contract for each mutation family.
Choose a policy for production v3 failures: restore the in-memory change, or
retain a clearly marked unsaved state with a defined recovery path. For small
edits, a prepared change containing the prior value is simpler than copying the
entire tree. Keep destructive subtree operations in their own design step.

**Benefit.** A caller can distinguish validation failure, accepted pending work,
durable success, and failed persistence. Later unrelated commits should not
silently decide the fate of a change the UI reported as failed.

**Tests and acceptance.** Inject a commit failure after rename, favorite, tag,
settings, and deletion changes. Inspect the live tree, cold-reopened tree, and
the result of a subsequent successful mutation. Test no-op operations too.
Preserve current behavior in characterization tests before deliberately changing
it. Do not label this entire item a behavior-preserving refactor or promise
rollback after an already-accepted asynchronous legacy write.

### R04 Encapsulate SQL transaction lifetime without hiding SQL

**Evidence.** [v3_db.cpp](../../src/vault/v3_db.cpp#L734) repeats
`BEGIN IMMEDIATE`, status chaining, `COMMIT`, and rollback across snapshot sync,
conversion batches, deletion, categories, and conversion markers.
`sync_metadata` already has a local rollback lambda and a critical explicit
guard that converts a non-SQL validation failure into `InvalidArgument`.
Some other error branches, including
[begin_conversion](../../src/vault/v3_db.cpp#L1700), obtain SQLite's last error
after issuing rollback. Error preservation deserves a regression before cleanup.
The file already has a `Statement` RAII wrapper; another statement wrapper is
unnecessary.

**Change.** Add a small transaction guard that records begin success, rolls back
an unfinished transaction on destruction, and exposes a fallible explicit
`commit()`. Capture the operation's original status before cleanup. Extract
named snapshot stages for nodes, staged objects, tags, settings, and saved
searches, while retaining one enclosing transaction and explicit SQL.

**Benefit.** Early returns cannot accidentally omit rollback, and reviewers can
see both transaction scope and the reason an operation failed. This does not
require an ORM, a new dependency, or a generic SQL query framework.

**Tests and acceptance.** Extend [database tests](../../tests/vault/test_v3_db.cpp)
for failure in each snapshot stage, commit failure, and preserved error status.
Keep `v3_db_invalid_snapshot_preserves_committed_tree` and
`v3_db_conversion_batch_rolls_back_nodes_objects_and_mapping_together` intact.
Generation, node rows, object references, and conversion mappings must either
all advance or all retain their previous committed state.

### R05 Put persisted query and ordering logic in the domain layer

**Evidence.** [vault.cpp](../../src/vault/vault.cpp#L29) includes
`ui/advanced_search_model.h` and `ui/gallery_sort.h`;
[vault_search.h](../../src/vault/vault_search.h#L16) returns `ui::TagTally` and
accepts `ui::AdvancedQuery`. These helpers are largely headless, so this is not
an SDL runtime dependency. It is nevertheless contrary to the documented
direction of ownership: storage operations and persisted saved-search encoding
depend on types named and located in the UI layer.

**Change.** Move query representation, evaluation, encoding, and domain tag
tallies into cohesive vault/domain modules. Separate child ordering from UI
labels and next/previous sort-key controls. Keep those presentation helpers in
UI. Move `VaultSearch` implementation out of `vault.cpp` along the same boundary,
using narrow access rather than distributing its helpers across unrelated files.

**Benefit.** Storage tests and non-UI callers can use the domain without importing
UI models. This is a meaningful decomposition of `vault.cpp`, unlike converting
methods into free friends solely to meet a method-count threshold.

**Tests and acceptance.** Preserve serialized saved-query bytes, case-insensitive
matching, relevance ordering, stable sorting, and folders-first mixed-gallery
ordering. Use the existing advanced-search model, vault-search, tag-overview,
and gallery-sort tests. Keep compatibility fixtures for saved searches and run
`scripts/gen.sh` after moving source files. Update the vault, UI-model, and
conventions memories to describe the resulting boundary.

### R06 Share media preparation before storage dispatch

**Evidence.** [staging.cpp](../../src/vault/staging.cpp#L21) has a local
`decode_and_thumbnail` helper. The decode pool in
[import_queue.cpp](../../src/ui/import_queue.cpp#L155) independently performs
decode, dimensions/format extraction, thumbnail encoding, and animation
detection. `stage_image` separately handles precomputed versus inline
preparation inside its backend branches. `staging.cpp` consequently depends on
image and media codecs even though it lives in the storage directory.

**Change.** Introduce one image-preparation operation returning dimensions,
format, animation state, and a `SecureBytes` thumbnail. Use it in the decode pool
and the synchronous fallback. Resolve prepared-versus-generated metadata before
storage dispatch, then pass the result to explicit legacy/v3 writers. Preserve
the public facade while moving codec orchestration toward the image/media or
application boundary. Apply the same pattern to video only after the image path
has demonstrated value.

**Benefit.** Thumbnail quality, format handling, and animation decisions acquire
one implementation. Backend code can concentrate on authenticated publication
instead of choosing decode policy.

**Tests and acceptance.** Compare inline and precomputed imports of valid JPEG,
animated WebP, undecodable input, and no-thumbnail results on both backends.
Retain intentional storage of supported-name originals whose decode fails.
Cover cancellation and allocation failure; avoid a temporary `std::vector`
copy of plaintext. Use [staging tests](../../tests/vault/test_staging.cpp),
[image tests](../../tests/image/test_image.cpp), and v3 prestaging tests.

### R07 Give staged import records explicit alternatives

**Evidence.** [ImportQueue::StagedRecord](../../src/ui/import_queue.h#L132) encodes
three meanings with an optional node and an empty/nonempty tag: ensure a
gallery, attach media, or collect a tag for review. Its older comment says
nullopt means gallery-only, which is no longer the complete contract.
[drain](../../src/ui/import_queue.cpp#L677) and
[abort_and_flush](../../src/ui/import_queue.cpp#L369) each interpret these
sentinels and apply records. Both discard some operation results; the normal
drain recognizes success and collisions but does not explicitly classify every
other attach result. The shutdown path intentionally discards unreviewed tags.

**Change.** Use a tagged record, such as a variant of `EnsureGallery`,
`AttachMedia`, and `ReviewTag`, with shared task identity. Factor out record
application with an explicit normal-drain versus shutdown policy and a typed
outcome for counters, errors, and screen refresh. Keep queue scheduling, record
application, and commit policy as distinct responsibilities. Move commit work
out of the queue mutex where its ordering can be preserved and tested; v3
snapshot calls currently perform synchronous I/O from the locked policy section.

**Benefit.** Impossible record combinations disappear, and the two drain paths
share mechanics without losing their different tag-consent behavior.

**Tests and acceptance.** Cover interleaved directory/media/tag records,
collisions, missing destinations, canceled tasks, failed attachment, and failed
final commits. Preserve file order, lookahead byte/item bounds, password wiping,
and the main-thread-only index tree. Verify counters against cold-reopened
content, not just task completion. Extend
[queue tests](../../tests/ui/test_import_queue.cpp), including the existing
tag-review cancellation and nested-gallery reopen cases.

### R08 Extract gallery import interaction as a cohesive component

**Evidence.** [GalleryGrid](../../src/ui/gallery_grid.h#L113) owns browse state
alongside archive naming, password entry, volume-set confirmation, file/folder
picker polling, export, transfer, and rendering.
[handle_overlay_event](../../src/ui/gallery_grid.cpp#L1235) establishes an ordered
modal chain. Existing [CollectionBatchOps](../../src/ui/collection_ops.h) already
shares selection operations across collection screens, and `tile_thumb` already
shares thumbnail drawing. The grid's richer flows should not be replaced blindly
with either component.

**Change.** First extract an import-interaction controller that owns pending
picker results, naming/password/volume prompts, and queue submissions. Give it a
narrow gallery context and explicit completion results. Keep layout, selection,
and viewport state in the grid. After this works, evaluate whether the common
parts of export/delete/transfer can use CollectionBatchOps or a smaller shared
operation controller.

**Benefit.** Import workflows can be reasoned about without reading grid
rendering, and grid changes no longer require understanding every archive prompt.
Avoid a screen base class with numerous virtual hooks or an all-purpose modal
framework.

**Tests and acceptance.** Preserve modal priority, Escape/right-click behavior,
archive password retries, canceled pickers, multi-volume ordering, and which pane
may consume shared picker results. Keep dialogs App-owned because their callbacks
outlive individual screens. Use pure transition tests plus the existing
dual-gallery, transfer, consent, and import suites, followed by focused GUI checks.

### R09 Reuse job completion mechanics while retaining job policy

**Evidence.** [FileOpJob](../../src/ui/file_op_job.h#L58),
[MigrationJob](../../src/ui/migration_job.h#L58),
[UnlockJob](../../src/ui/unlock_job.h), and
[LegacyConversionJob](../../src/ui/legacy_conversion_job.h#L15) each coordinate
thread lifetime, active/done flags, and a one-time outcome. Their semantics
differ: unlock's KDF is not interruptible, migration cancellation commits partial
work without a completion watermark, and conversion must verify and reopen its
destination before promoting it. A finished-but-uncollected file operation
remains active to protect borrowed vaults and cached UI pointers.

**Change.** Pilot a small composed completion/outcome holder in two matching jobs.
Keep cancellation callbacks, progress models, credentials, and worker algorithms
in each concrete job. Reuse only the launch/publish/join/collect mechanics that
actually match. Preserve the existing thread-creation-failure handling in unlock
and conversion rather than assuming all launches succeed.

**Benefit.** The ownership protocol is tested once as well as through concrete
jobs. This should not become a generic executor or a job inheritance hierarchy.

**Tests and acceptance.** Test second-start rejection, launch failure, completion
without collection, collect-once, cancel-before-completion, and destruction during
work. Retain [migration abort tests](../../tests/ui/test_migration_job.cpp),
[file-operation tests](../../tests/ui/test_file_op_job.cpp), and warm-vault
ownership tests. Never release exclusivity simply because the worker's done flag
became true. If the helper requires many policy switches, keep the jobs separate.

### R10 Distinguish media location from cache identity

**Evidence.** [ImageMeta and VideoChunk](../../src/vault/index.h#L58) retain
legacy offset/length fields. For directory vaults,
[ReadSession::rebuild_object_index](../../src/vault/v3_read_session.cpp#L259)
populates some offset fields with `object_cache_identity` values; video chunks
can instead carry frame sequence placeholders. [ChunkRef](../../src/vault/chunk_ref.h)
combines physical spans with authentication identity, and
[read_thumb_span](../../src/vault/vault.cpp#L1290) dispatches by backend and
domain. The same numeric field therefore has different meanings depending on
the backend.

**Change.** Introduce distinct in-memory types for thumbnail cache keys and
readable media references. Keep a legacy-span adapter and a directory
node/role/frame adapter behind the read facade. Migrate thumbnail workers and
duplicate scanning first; preserve old serialized layouts through explicit
adapters. Consider stable node handles for long-lived UI state only as a later,
separately justified step.

**Benefit.** Code cannot accidentally use a cache identity as a file offset or
treat a zero legacy span as proof that a v3 frame is absent. This clarifies the
representation of behaviors already protected by regression tests; it does not
establish a current read bug.

**Tests and acceptance.** Cover fresh imports and reopened objects, image
original versus thumbnail roles, video frame sequences, cache invalidation after
poster replacement, and legacy compaction. Keep authenticated identity and
role/sequence substitution tests intact. No `.osv` format change, node-ID
reminting, or weakening of context-bound AEAD belongs in this refactor.

### R11 Consolidate narrow FFmpeg resource operations

**Evidence.** [VideoDecoder::reset](../../src/media/video_decoder.cpp#L71),
`seek`, and `seek_demux_only` repeat packet-queue release and audio-output
clearing. [decode_poster_rgb](../../src/media/video_decoder.cpp#L557),
[GifDecoder](../../src/media/gif_decoder.cpp#L233), and
[duplicate-video hashing](../../src/ui/dup_video_sig.cpp#L192) each construct
padded packed-pixel destinations. The project already has `FrameConverter` for
shared frame conversion and `ffmpeg_secure` for reference-count-aware storage.

**Change.** First extract packet-queue cleanup or use a local owning packet type
with an FFmpeg deleter. Separately share a checked packed-pixel layout/allocation
helper if it makes all three scratch sites simpler: aligned stride, total bytes,
tail padding, and four-element plane/stride arrays. Keep seek-state changes,
colorspace decisions, and each caller's output conversion explicit.

**Benefit.** Allocation and cleanup invariants have fewer independent
implementations. A second general frame-conversion framework would add
complexity and should be avoided.

**Tests and acceptance.** Preserve software and hardware-fallback playback,
seek/EOF/audio-drain behavior, and reference-counted final-release wiping.
Exercise odd widths and decoded dimensions that differ from container headers.
Run the existing swscale canary procedure in
[Phase 80](../roadmap/phase-80-poster-heap-overrun.md); ASAN alone cannot detect
stores performed by uninstrumented vendored assembly. Never replace padded
scratch with a tight allocation or wipe a frame when another reference owns it.

### R12 Keep preference simplification small

**Evidence.** [AutoplayPref](../../src/platform/autoplay_pref.cpp),
[GalleryViewPref](../../src/platform/gallery_view_pref.cpp), and
[ClipboardPref](../../src/platform/clipboard_pref.cpp) repeat missing-file
handling, first-line reads, and CRLF removal. Their writes already use
[atomic_write_file](../../src/platform/atomic_write.h).
[HwAccelPref](../../src/platform/hwaccel_pref.cpp) has a different multiline,
unknown-key-tolerant format.

**Change.** Optionally share a first-line reader or CR stripping helper, leaving
defaults and slug parsing in each preference class. Remove redundant includes
when touching those files. Keep multiline formats separate.

**Tests and acceptance.** Retain missing/empty/invalid input, CRLF, exact output,
and invalid-location tests for each setting. Preserve filenames, defaults, and
compatibility with the old hardware/software flags. Do this only if call sites
become shorter and clearer; a registry, schema system, template hierarchy, or new
configuration dependency is disproportionate. Do not reuse preference-file
writing for encrypted-object publication or consent-gated plaintext export.

### R13 Reuse build source groups before redesigning the build

**Evidence.** [premake5.lua](../../premake5.lua#L420) combines directory globs for
core modules with a long explicit list of UI/platform implementations in
`osv_tests`; the application has its own source selection. This makes extraction
work easy to compile in the application but omit from tests. Conversely,
`scripts/test.sh` deliberately builds both binaries to catch application-only
compile failures. `src/app/app.cpp` itself is not in the test target's explicit
App source selection.

**Change.** Define named source groups for headless domain code, rendering
support, and screen adapters, then use those groups in the existing Premake
targets. Start with group reuse rather than introducing a new build system or
many libraries. A shared static library can be evaluated later if measured
recompilation costs justify it and compile definitions remain compatible.

**Tests and acceptance.** Compare source membership before and after the change;
build both targets in Debug, Release, no-AV, and sanitizer configurations. Retain
SQLCipher/OpenSSL static-link checks, optional codec guards, fixture definitions,
and generation/restoration of normal build files. Regenerate
`compile_commands.json` through `scripts/gen.sh`. No CI matrix reduction is
part of this simplification.

### R14 Make test fixtures identify their storage backend

**Evidence.** [ziptest::make_vault](../../tests/ui/zip_test_helpers.h#L81) calls
legacy `Vault::create` and discards its return value. Many import and staging
tests reuse that helper. The queue already has an explicit v3 durability test,
and [v3 read-session tests](../../tests/vault/test_v3_read_session.cpp) cover
prestaging and cross-backend mutation behavior. The issue is implicit backend
selection and uneven scenario coverage, not an absence of v3 tests.

**Change.** Provide explicitly named checked fixtures for directory vaults,
legacy writable fixtures, and reopened read-only legacy vaults. Share fixture
setup, then run selected behavior contracts against every supported backend
mode. Keep legacy slot/crash/format tests legacy-specific. Make synthetic
invalid JPEG fixtures distinguishable from real decodable-image fixtures.

**Benefit.** A test's name and setup reveal whether it exercises production
storage, compatibility reads, or a retained fixture writer. Silent setup failure
does not obscure later assertions.

**Tests and acceptance.** Expand the v3 matrix around cancellation, collisions,
folder/nested archive imports, tag review, transfer, and failure at final commit.
Close workers and vault handles before fixture cleanup, reopen with credentials,
and compare bytes and metadata. Use deterministic worker barriers where practical
instead of sleeps; any polling helper should report timeout explicitly. Avoid
multiplying every expensive codec test when a smaller contract set proves the
backend behavior.

### R15 Bring current-state documentation back into agreement

**Evidence.** The opening comment in [index.h](../../src/vault/index.h#L3) says
galleries contain sub-galleries or images, never a mixture, contradicting Phase
46 and the current implementation. [ImportQueue's header](../../src/ui/import_queue.h#L44)
describes an embedded CommitLane although App owns it. Its abort comment says
the router is uninstalled there, while current shutdown leaves it installed
until the lane stops. [module/app memory](../../.serena/memories/module/app.md#L118)
describes an older overlay ordering than
[dispatch_overlay_event](../../src/app/app.cpp#L993). The
[conventions memory](../../.serena/memories/conventions.md) says vault depends on
crypto only, despite the dependencies documented in R05 and R06.

**Change.** Correct false current-state claims in headers and memories. Keep
security rationale and historical failure explanations that protect subtle
invariants. Move phase-by-phase narrative out of canonical module memories where
the roadmap already records it. Describe exceptions to intended boundaries
accurately until the code moves; do not document a desired architecture as if it
already exists.

**Tests and acceptance.** Check affected symbols, paths, ownership, and event
order against source. Run `serena memories check` if its CLI is available, or
review affected memory links and symbols directly. Updating comments does not
authorize changing mixed-gallery behavior, overlay priority, or commit timing.

## Constraints that must survive every refactor

1. Preserve `SecureBytes`, `SecureString`, secure allocator behavior, and wiping
   on reallocation/final release. Standard containers may remain appropriate for
   ciphertext and non-secret metadata; do not replace secure plaintext ownership
   merely to unify APIs.
2. Keep fresh nonces, authenticate-before-use, context-bound record identities,
   and the existing container formats. Do not combine a representation cleanup
   with a cryptographic or format migration.
3. Retain the main-thread index-tree model during concurrent import and exclusive
   ownership during blocking jobs. Refresh cached node pointers before normal
   rendering resumes after mutations that can invalidate them.
4. Preserve staged-object protection from GC, immutable publication, writer
   leases, object/directory sync ordering, and SQL transaction atomicity. Legacy
   slot-swap durability remains a separate storage protocol.
5. Keep the explicit plaintext-export consent gate and pinned, no-follow output
   handles. Configuration writes, encrypted-object publication, and plaintext
   export have different contracts and should retain distinct APIs.
6. Preserve production read-only legacy access and copy-based conversion. The
   legacy writer still supports fixtures and compatibility tests; delete it only
   after a separate reachability and fixture-migration assessment.
7. Keep codec fallback, no-AV compilation, final-reference FFmpeg wiping, and
   swscale row/tail padding. Do not simplify away the documented opaque codec and
   driver limitations or F1 degraded status.

Existing `tile_thumb`, `CollectionBatchOps`, `FrameConverter`, `atomic_write_file`,
secure allocation, and pure layout/selection models are useful foundations.
Extend them only where the caller contracts match. Avoid a whole-program rewrite,
a general service locator, a universal worker scheduler, or broad inheritance
introduced solely to reduce line counts or satisfy Sonar method-count limits.

## Proposed sequence of reviewable changes

| Step | Scope | Completion evidence |
| --- | --- | --- |
| 1 | R14 explicit fixtures and targeted characterization | Production v3 scenarios cold-reopen successfully; legacy fixtures are named explicitly |
| 2 | R15 factual documentation corrections | Current ownership, mixed galleries, event priority, and dependency exceptions agree with code |
| 3 | R05 domain model relocation | No UI query/tally dependency from the affected vault API; saved-query bytes and ordering unchanged |
| 4 | R01 session teardown owner | One shared teardown ordering; policy differences covered by lifecycle tests |
| 5 | R02 commit coordinator | Callers can distinguish pending acceptance from durability; v3 needs no legacy worker |
| 6 | R04 SQL transaction guard and named snapshot stages | Injected failures preserve prior rows, generation, mappings, and original error status |
| 7 | R03 mutation failure contract, one mutation family per PR | Live state, reopened state, and later commits follow the chosen documented policy |
| 8 | R06 media preparation, then R07 import records | Inline/prepared parity; attachment, counters, ordering, and tag consent preserved |
| 9 | R08 gallery import controller | Prompt and pane flows preserved; grid no longer owns import orchestration details |
| 10 | R09 job protocol and R11 narrow media helpers in separate PRs | Join/collection and secure final-release behavior preserved |
| 11 | R10 typed read/cache identities | Backend-specific readers cannot confuse cache keys, spans, and frame references |
| Optional | R12 preference helper and R13 source groups | Less duplicated mechanics without additional framework or build assumptions |

Use one PR per coherent change, keeping implementation, tests, ROADMAP, and
affected module memories together. For code changes, follow the repository's TDD
flow: characterize behavior first, implement the extraction, then run Debug and
Release suites and the no-AV build. Run ASAN/UBSan for crypto, vault, and memory
changes; run TSan for changed worker/ownership paths. Restore normal build files
after alternate configurations. Retain targeted canary or valgrind checks where
sanitizers cannot observe the relevant fault.

CI must pass and PR-posted SonarCloud findings must be resolved before delivery.
Do not install or authenticate a Sonar CLI. The owner merges; agents stop at a
ready PR. After the owner reports merging, verify roadmap/docs on `origin/main`
and update affected memories according to AGENTS.md.

## How to judge whether the work simplified the code

Measure fewer independent lifecycle sequences, explicit commit outcomes, fewer
cross-layer includes, and fewer sentinel-based state combinations. Record those
changes in each PR with the behavior tests that protect them. Track production
facade latency and memory use only when the change claims a performance benefit,
using the same fixtures and configuration before and after.

A successful extraction gives each operation a clear owner and fewer
preconditions to remember. More files, fewer methods per class, or lower physical
line counts alone do not establish that improvement.
