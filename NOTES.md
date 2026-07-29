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
