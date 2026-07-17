## Phase 42 — ThreadSanitizer CI leg ⬜

**Goal:** Phase 41 introduced this project's first genuinely concurrent data
pipeline outside the already-proven `image::DecodeWorker` pattern
(`media::VideoDecodeWorker`, a producer/consumer packet-and-frame queue
across two threads). The existing CI only runs ASAN/UBSan (`scripts/test.sh --asan`),
which does not catch data races. This phase adds a ThreadSanitizer leg,
mirroring how the ASAN leg needed its own sanitizer-built vendored-deps
prefix (`vendor/codecs-prefix-asan`, `scripts/build_codecs.sh --asan`):
TSan and ASAN cannot be combined in one binary, so this requires a parallel
`vendor/codecs-prefix-tsan` prefix, a new `--tsan` premake option, and a new
CI job. Deferred out of Phase 41 (per that phase's design spec) so the
video-decode-threading change could land and be reviewed on its own —
this phase's job is purely to point TSan at the code Phase 41 already
shipped (and any future concurrent code) once it lands.

### Tasks
- TBD at implementation time — this stub exists so the roadmap link from
  Phase 41 resolves; scope this properly (build script changes, CI job
  definition, expected TSan runtime, suppression list for any vendored C
  library false positives) when the phase is picked up.

### Acceptance criterion
TBD — defined when this phase is scoped.
