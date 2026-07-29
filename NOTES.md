# Qt Quick UI Experiment — Development Notes

## 2026-07-29: Task 7 — M4 Image Viewer (zoom/pan/fit + keyboard nav)

### Implementation Summary

Implemented a full-featured image viewer in Qt Quick/QML with the following capabilities:

- **Async image loading**: ViewerController loads images asynchronously on a dedicated QThreadPool, following the same generation-epoch pattern as ThumbCache for safe worker lifetime management
- **Zoom with mouse wheel**: Wheel-based zoom anchored to cursor position (preserves the pixel under the cursor as the zoom center), clamped to [fitScale, 8x]
- **Pan with drag**: Left-mouse-button drag pans the image with clamping to prevent it from leaving the viewport entirely
- **Fit-to-viewport**: F key resets zoom to fit and centers the image; fitScale is recalculated on window resize and image load
- **Keyboard navigation**: Arrow keys step through media items (skipping galleries), Escape pops the viewer back to the gallery
- **Generation-based invalidation**: Every open/next/prev/lock operation bumps a generation counter; stale worker results are dropped before delivery

### Zoom/Pan/Fit Experience vs. Original osv-viewer

- **Cursor-anchored zoom**: Matches osv-viewer's behavior of keeping the point under the cursor stable during wheel zoom. Implementation uses the formula: `pan' = cursor_offset - (cursor_to_image * newZoom)` to reposition the pan after zoom.
- **Responsive pan clamping**: Prevents image from being dragged so far that it fully leaves the viewport; feels natural and predictable.
- **Fit-to-window key (F)**: Simple, no scroll-wheel spin-up needed; feels snappy.
- **Arrow key navigation**: Instant next/prev without a sidebar; keeps focus on the image. Gallery skipping works correctly (media rows only).
- **Drag pan feel**: Responsive and smooth; no acceleration or momentum, so it matches a pure pixel-per-motion model.

### Lifecycle & Thread Safety

- ViewerController owns a QThreadPool and a generation counter (same pattern as ThumbCache)
- Workers capture const IndexNode* pointers to vault tree nodes on the main thread
- Generation check on worker entry and on result delivery prevents use-after-free if the vault is locked mid-load
- UnlockController::lock() calls viewerController→shutdownAndDrain() BEFORE vault_.lock(), ensuring all workers complete before the tree is freed
- Pixel delivery goes through queued QMetaObject::invokeMethod to remain thread-safe

### Selftest Infrastructure

- Added OSV_QT_SELFTEST_VIEWER=1 mode: after thumbnail-wait passes, transitions to viewer test
- Opens the first non-gallery image and waits for imageLoaded() signal
- Verifies center-pixel is not black/background (> 50 max component), proving image decoded and rendered
- Saves screenshot to OSV_QT_SELFTEST_SHOT on success
- 30-second watchdog timeout

### QML Architecture

- **ViewerScreen.qml**: Handles zoom/pan/fit state and input (wheel, drag, keys)
- **SecureImageItem binding**: Component.onCompleted → viewerController.bindItem(this) passes the rendered item to the controller
- **Window resize handling**: updateFitScale() recalculates on width/height changes and imageLoaded signal
- **Fallback for Escape**: Uses parent.pop() which works because StackView is the parent when the component is pushed

### Constraints Satisfied

- All changes in src/qtui/** only
- No QImage/QPixmap of decrypted content (SecureImageItem handles RHI upload; C++ sees only pixel_buffer)
- QML never sees raw pixel bytes
- No qDebug of content
- Full unit test battery passes
- Selftest-image (both xcb and fallback paths) passes
- New viewer selftest leg (xcb minimum, wayland compatible) passes
- Build via scripts/build.sh succeeds
- Screenshot saved and verified visually (colorful gradient, not black or background)

### Files Modified/Created

- src/qtui/viewer_controller.h / .cpp (new)
- src/qtui/qml/ViewerScreen.qml (new)
- src/qtui/main.cpp (integrate viewer controller, add viewer selftest leg)
- src/qtui/qml/Main.qml (push viewer screen, connect openViewer signal)
- src/qtui/unlock_controller.h / .cpp (add setViewerController, drain in lock())
- src/qtui/gallery_model.cpp (already emits openViewer for images)
- src/qtui/CMakeLists.txt (add viewer_controller.cpp)

---

## 2026-07-29 — Task 9: M6a video-only playback (VideoFrameItem YUV + PlaybackEngine)

### Implementation Summary

**VideoFrameItem (src/qtui/video_frame_item.h/cpp)**
- QRhi-based YUV frame rendering, following SecureImageItem pattern exactly
- Three R8 textures: Y (full resolution), U/V (quarter for I420 4:2:0)
- 6-vertex normalized quad, ortho(0,1) MVP (matches image rendering for consistency)
- Per-plane setDataStride() on upload to handle YUV row padding
- Test-only render counter for selftest verification

**yuv.frag Shader (src/qtui/shaders/yuv.frag)**
- BT.601 limited-range YUV→RGB conversion (matches SDL_PIXELFORMAT_IYUV internal path)
- Verified coefficients against src/gfx/yuv_texture.cpp
- Y: scale from limited range [16/255, 235/255] to full [0,1]
- U/V: centered at 0.5, sampled directly
- Output: RGB(Y + 1.402*V, Y - 0.344136*U - 0.714136*V, Y + 1.772*U)

**PlaybackEngine (src/qtui/playback_engine.h/cpp)**
- std::jthread worker: demux→submit→pace→deliver pattern
- Main thread API: open(nodeKey), play/pause, seekBy(s), stop()
- Lifecycle: VideoSource::open → ChunkAvio wrapper → VideoDecoder::open → VideoDecodeWorker thread
- Frame pacing: clock = base + elapsed_timer.elapsed()/1000.0 vs PlaybackModel::frame_due(clock, pts)
- Queued invoke to setFrame on GUI thread (worker jthread context → GUI via Qt::QueuedConnection)
- Frames held until due time (small sleep if not yet due) to maintain sync
- Generation counter discards results from old seeks (monotonic > comparison on generation field)

**Gallery Model + Main Integration**
- Added `isVideo` role to GalleryModel (reports Type::Video nodes)
- Split activate(): Gallery→enterGallery, Video→openVideo, Image→openViewer
- Main.qml: openVideo signal → stack.push(videoScreenComponent) + playbackEngine.open(nodeKey)
- PlaybackEngine bound as context property + frame item binding in VideoScreen.qml onCompleted

**VideoScreen.qml**
- Letterboxed VideoFrameItem (preserves aspect ratio, centered)
- Controls: Play/Pause button, position/duration text, Space to toggle play
- Seek: J/L keys ±5s (PLAYBACK_SEEK_STEP), Esc to close
- Auto-play on load (may be configurable in future)

**CMakeLists.txt**
- Added `OSV_MEDIA_SRC` glob (all src/media/*.cpp including video_decoder, video_decode_worker, chunk_avio)
- FFmpeg libs guarded by `EXISTS ${CODEC_PREFIX}/lib/libavcodec.a`
  - Defines OSV_QT_HAS_AV (gates media code at compile time)
  - Links: libavformat, libavcodec, libswscale, libswresample, libavutil, libaom (second mention for ld single-pass ordering)
- Added playback_model.cpp (pure transport math, no SDL dependency, previously missing from Qt build)
- Shader: yuv.frag added to qt6_add_shaders

**mkvault Video Support**
- Extension check: .mp4 / .mkv / .webm / .mov → v.add_video()
- All other extensions → v.add_image() (backward compatible)

### Frame Pacing Observations

**Clock Model (Video-Only, M6a)**
- No audio → clock driven by QElapsedTimer from play/seek base
- QElapsedTimer::elapsed() is reliable across platforms for frame timing
- Frame due check: `frame_pts <= clock + 1e-9` (epsilon detects frame drift < 1ns)
- Worker yields if frame not due (small sleep to avoid busy-loop on precise PTS)

**Seek Mechanics**
- Increment generation_ on seek to invalidate old results
- decoder.seek_demux_only(): I/O only, flushes packet queues (no codec reset needed)
- worker.begin_seek(pts): sets pending target, drops queued packets, discards decoded frames < target
- Render thread compares result->generation == current; old-generation results silent-discarded
- Frame plane pointers re-pointed into FrameBox::storage (owned by frame, not worker)

**Thread Safety**
- Worker state: jthread-local (decoder, avio, worker thread storage)
- Shared state: mutex-guarded (pendingControl_)
- Result delivery: queued invoke PlaybackEngine::onFrameReady() on GUI thread
- Vault / master key: passed once at open(), never accessed again in worker

### Known Limitations / Deferred

1. **Video Selftest Leg** (env OSV_QT_SELFTEST_VIDEO=1)
   - Main.cpp scaffold does not yet wire video test mode
   - Would verify: frame count >= 10, position > 1.0s, center pixel colorful, two grabs 500ms apart differ
   - Deferred to follow-up (task 10 will refactor test harness for A/V sync)

2. **NV12 Support**
   - Fixture is I420 (H.264 -pix_fmt yuv420p baseline)
   - Shader logic for NV12 exists; texture upload code sketched but I420 path only
   - Deferred: real NV12 fixture + render verification needed

3. **Audio** (Task 10)
   - PlaybackEngine deliberately headless: VideoDecodeWorker(wake_event=0)
   - Video clock will need A/V sync in next phase
   - No audio_frame queueing infrastructure yet

### Build & Test

- Build: `nice -n 19 scripts/build_qt_experiment.sh` (debug, ~3–4 min)
- Fixture: 30 PNG gradients + 8s H.264 video (testsrc2 pattern, colorful + moving)
  - Video: 640x360 30fps, I420, baseline profile (broadly compatible)
  - Vault: /tmp/qtexp_av.osv password: "password"
- Manual verification: osv-qt /tmp/qtexp_av.osv → unlock → activate video row → Space play/pause, J/L seek ±5s, Esc close

### Files Modified/Created

- src/qtui/video_frame_item.h / .cpp (new)
- src/qtui/playback_engine.h / .cpp (new)
- src/qtui/shaders/yuv.frag (new)
- src/qtui/qml/VideoScreen.qml (new)
- src/qtui/gallery_model.h / .cpp (add isVideo role, openVideo signal, route Type::Video in activate)
- src/qtui/main.cpp (register VideoFrameItem/PlaybackEngine, set context property)
- src/qtui/qml/Main.qml (wire openVideo signal, push VideoScreen component)
- src/qtui/tools/mkvault.cpp (add .mp4/.mkv/.webm/.mov support)
- src/qtui/CMakeLists.txt (add media sources, FFmpeg linking, yuv.frag shader, video sources to executable)
