import QtQuick
import QtQuick.Controls
import Osv 1.0

Rectangle {
    id: root
    color: themePalette.imgBg
    focus: true

    // Zoom and pan state
    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    property real fitScale: 1.0

    // Zoom modes: 0=Fit (entire image visible), 1=FillScroll (image covers viewport, pan to see)
    property int zoomMode: 0

    // Fullscreen state
    property bool isFullscreen: false
    property int fullscreenExitChain: 0  // 0=normal, 1=first right-click (exit fullscreen), 2=second right-click (return to gallery)

    // Slideshow state
    property bool slideshowRunning: false
    property real slideshowDwell: 3.0  // seconds per image
    property real slideshowElapsed: 0.0  // accumulated time toward next advance
    property int slideshowFadeOutIndex: -1  // index of image fading out (or -1)
    property real slideshowFadeProgress: 0.0  // 0=new image, 1=old image fully out

    // Help groups for F1 help popup
    property var helpGroups: [
        {
            title: "Image Viewer",
            entries: [
                { keys: "Esc", description: "Close viewer" },
                { keys: "Arrow Keys/Mouse Drag", description: "Pan image" },
                { keys: "+/-/Scroll", description: "Zoom in/out" },
                { keys: "F", description: "Cycle zoom modes (Fit ↔ FillScroll)" },
                { keys: "1", description: "Reset to fit" },
                { keys: "Shift+F", description: "Toggle fullscreen" },
                { keys: "Space", description: "Play/pause animated image" },
                { keys: "T", description: "Toggle thumbnail strip visibility" },
                { keys: "P", description: "Toggle slideshow" },
                { keys: "[/]", description: "Adjust slideshow dwell time" }
            ]
        }
    ]

    // Helper: get total count of media items in current gallery (skip galleries)
    function getMediaCount() {
        let count = 0;
        for (let i = 0; i < galleryModel.rowCount(); ++i) {
            const idx = galleryModel.index(i, 0);
            const isGallery = galleryModel.data(idx, GalleryModel.IsGalleryRole);
            if (!isGallery) {
                ++count;
            }
        }
        return count;
    }

    // Helper: get current media index (skip galleries, return -1 if not an image)
    function getCurrentMediaIndex() {
        const currentRow = viewerController.currentIndex;
        let mediaIndex = 0;
        for (let i = 0; i < currentRow; ++i) {
            const idx = galleryModel.index(i, 0);
            const isGallery = galleryModel.data(idx, GalleryModel.IsGalleryRole);
            if (!isGallery) {
                ++mediaIndex;
            }
        }
        // Verify current row is not a gallery
        const currentIdx = galleryModel.index(currentRow, 0);
        const isGallery = galleryModel.data(currentIdx, GalleryModel.IsGalleryRole);
        return isGallery ? -1 : mediaIndex;
    }

    // Helper: check if current item is a video
    function isCurrentItemVideo() {
        const currentRow = viewerController.currentIndex;
        if (currentRow < 0) return false;
        const idx = galleryModel.index(currentRow, 0);
        return galleryModel.data(idx, GalleryModel.IsVideoRole) === true;
    }

    // Helper: get zoom percentage (1-800%)
    function getZoomPercentage() {
        return Math.round(zoom * 100);
    }

    // Helper: build header text "name · N/M · zoom%"
    function buildHeaderText() {
        const name = viewerController.imageName;
        const mediaCount = getMediaCount();
        const mediaIndex = getCurrentMediaIndex();
        const zoomPct = getZoomPercentage();

        if (mediaIndex < 0 || mediaCount === 0) {
            return name;  // fallback if index unavailable
        }

        return name + " · " + (mediaIndex + 1) + "/" + mediaCount + " · " + zoomPct + "%";
    }

    // Calculate fit-to-viewport scale
    function updateFitScale() {
        if (imageItem.sourceSize.width > 0 && imageItem.sourceSize.height > 0) {
            fitScale = Math.min(
                width / imageItem.sourceSize.width,
                height / imageItem.sourceSize.height
            );
            // In Fit mode, ensure zoom is at fitScale
            if (zoomMode === 0) {
                zoom = fitScale;
                resetPan();
            } else if (zoom < fitScale) {
                // In FillScroll mode, allow zooming out to fitScale minimum
                zoom = fitScale;
                resetPan();
            }
        }
    }

    function resetFit() {
        zoomMode = 0;  // Switch back to Fit mode
        zoom = fitScale;
        resetPan();
    }

    function cycleZoomMode() {
        // Videos stay in Fit mode only
        if (isCurrentItemVideo()) {
            return;
        }

        zoomMode = (zoomMode + 1) % 2;
        if (zoomMode === 0) {
            // Switching to Fit mode
            resetFit();
        } else {
            // Switching to FillScroll mode: zoom to cover viewport
            zoom = Math.max(fitScale, Math.max(
                width / imageItem.sourceSize.width,
                height / imageItem.sourceSize.height
            ));
            resetPan();
        }
    }

    function resetPan() {
        panX = 0;
        panY = 0;
    }

    // Clamp pan so image doesn't fully leave viewport
    function clampPan() {
        const imgW = imageItem.sourceSize.width * zoom;
        const imgH = imageItem.sourceSize.height * zoom;

        // Clamp: viewport can show any part from [0, imageSize]
        // Pan center must keep at least some image visible
        const maxPanX = Math.max(0, (imgW - width) / 2);
        const maxPanY = Math.max(0, (imgH - height) / 2);

        panX = Math.max(-maxPanX, Math.min(maxPanX, panX));
        panY = Math.max(-maxPanY, Math.min(maxPanY, panY));
    }

    // Slideshow functions
    function toggleSlideshow() {
        slideshowRunning = !slideshowRunning;
        if (slideshowRunning) {
            slideshowElapsed = 0.0;
        } else {
            slideshowFadeOutIndex = -1;
            slideshowFadeProgress = 0.0;
        }
    }

    function setSlideshowDwell(dwell) {
        // Clamp between 1 and 30 seconds
        slideshowDwell = Math.max(1.0, Math.min(30.0, dwell));
    }

    function adjustSlideshowDwell(delta) {
        setSlideshowDwell(slideshowDwell + delta);
    }

    function advanceSlideshow() {
        // Start a cross-fade and advance to next image
        slideshowFadeOutIndex = viewerController.currentIndex;
        slideshowFadeProgress = 0.0;
        slideshowElapsed = 0.0;
        viewerController.next();
    }

    // Slideshow timer tick (called at ~60 FPS)
    function tickSlideshow(dt) {
        if (!slideshowRunning) {
            return;
        }

        // Advance fade progress if in a fade
        if (slideshowFadeOutIndex >= 0) {
            slideshowFadeProgress += dt / 0.5;  // 0.5 second cross-fade
            if (slideshowFadeProgress >= 1.0) {
                slideshowFadeProgress = 1.0;
                slideshowFadeOutIndex = -1;
            }
        }

        // Accumulate time toward next advance
        slideshowElapsed += dt;
        if (slideshowElapsed >= slideshowDwell) {
            advanceSlideshow();
        }
    }

    // Fullscreen mode enter/exit
    function enterFullscreen() {
        isFullscreen = true;
        fullscreenExitChain = 0;
        // In a production system, set window flags to borderless here
        // For now, this is QML-side state tracking
    }

    function exitFullscreen() {
        isFullscreen = false;
        fullscreenExitChain = 0;
        // In a production system, restore window flags here
    }

    // Right-click chain: first click exits fullscreen, second returns to gallery
    function handleFullscreenRightClick() {
        if (!isFullscreen) {
            return;  // only in fullscreen
        }

        ++fullscreenExitChain;
        if (fullscreenExitChain === 1) {
            // First right-click: exit fullscreen, return to normal viewer
            exitFullscreen();
        } else if (fullscreenExitChain >= 2) {
            // Second right-click: return to gallery
            if (parent && typeof parent.pop === 'function') {
                parent.pop();
            }
        }
    }

    // Edge-click navigation (left/right edges in fullscreen)
    function handleEdgeClick(x) {
        if (!isFullscreen) {
            return;
        }

        const edgeWidth = 80;  // pixels from edge
        if (x < edgeWidth) {
            // Left edge: prev
            viewerController.prev();
            fullscreenExitChain = 0;  // reset right-click chain
        } else if (x > width - edgeWidth) {
            // Right edge: next
            viewerController.next();
            fullscreenExitChain = 0;  // reset right-click chain
        }
    }

    // Keyboard shortcuts
    property real thumbStripAnimDuration: 200  // ms
    property bool thumbStripVisible: true  // persisted via SessionState

    // Animated image playback controllers (created/wired by ViewerController when animated image opens)
    property var animController: null
    property var animatedImageLoader: null

    // Bind to controller
    Component.onCompleted: {
        viewerController.bindItem(imageItem);
        updateFitScale();
        slideshowTimer.start();
    }

    // Unbind from controller on destruction (prevents use-after-free if image load is in flight)
    Component.onDestruction: {
        viewerController.bindItem(null);
        slideshowTimer.stop();
    }

    // Slideshow timer: ticks at ~60 FPS to drive advance and fade animations
    Timer {
        id: slideshowTimer
        interval: 16  // ~60 FPS
        repeat: true
        onTriggered: {
            root.tickSlideshow(0.016);  // 16.67 ms per frame
        }
    }

    // Update fit scale when image loads or window resizes
    onWidthChanged: updateFitScale()
    onHeightChanged: updateFitScale()

    Connections {
        target: viewerController
        function onImageLoaded() {
            updateFitScale();
        }
    }

    // Image item: centered in viewport, scaled and panned
    SecureImageItem {
        id: imageItem
        width: sourceSize.width * zoom
        height: sourceSize.height * zoom
        x: root.width / 2 - width / 2 + panX
        y: root.height / 2 - height / 2 + panY
        visible: viewerController.imageName.length > 0
        opacity: 1.0 - root.slideshowFadeProgress

        onSourceSizeChanged: {
            root.updateFitScale();
        }
    }

    // Slideshow fade-out image (previous image fading out)
    // Note: In the current architecture, we don't have direct access to the previous image's pixels
    // This is a placeholder for the fade effect. Real implementation would need prev_image pixel buffer
    // For now, we use a semi-transparent overlay effect
    Rectangle {
        id: slideshowFadeOverlay
        anchors.fill: parent
        color: themePalette.imgBg
        opacity: root.slideshowFadeProgress * 0.3  // subtle fade during transition
        visible: root.slideshowFadeOutIndex >= 0
    }

    // Loading indicator
    Text {
        anchors.centerIn: parent
        text: "Loading..."
        color: themePalette.textDim
        font.pixelSize: 16
        visible: viewerController.loading
    }

    // Zoom with scroll wheel, anchored to cursor position
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse
        onWheel: {
            const oldZoom = root.zoom;

            // Zoom multiplier: 1.15 per wheel step
            let factor = 1.15;
            if (event.angleDelta.y < 0) {
                factor = 1 / factor;
            }

            const newZoom = Math.max(root.fitScale, Math.min(8, oldZoom * factor));
            if (newZoom === oldZoom) return;  // no change

            // Cursor position in scene coordinates
            const cursorX = event.position.x;
            const cursorY = event.position.y;

            // Image center offset from viewport center
            const imageCenterX = imageItem.width / 2;
            const imageCenterY = imageItem.height / 2;

            // Vector from viewport center to cursor
            const cursorOffsetX = cursorX - root.width / 2;
            const cursorOffsetY = cursorY - root.height / 2;

            // Vector from image center to cursor (old zoom)
            const cursorToImageX = (cursorOffsetX - root.panX) / oldZoom;
            const cursorToImageY = (cursorOffsetY - root.panY) / oldZoom;

            // Update zoom
            root.zoom = newZoom;

            // Reposition pan so cursor stays under the same image pixel
            // pan' = cursor_offset - (cursor_to_image * newZoom)
            root.panX = cursorOffsetX - (cursorToImageX * newZoom);
            root.panY = cursorOffsetY - (cursorToImageY * newZoom);

            root.clampPan();
        }
    }

    // Drag to pan
    DragHandler {
        acceptedButtons: Qt.LeftButton
        onTranslationChanged: {
            root.panX += translation.x;
            root.panY += translation.y;
            root.clampPan();
        }
    }

    // Mouse handlers for fullscreen edge clicks and right-click chain
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        onClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                // Left-click on edges for prev/next in fullscreen
                root.handleEdgeClick(mouse.x);
            } else if (mouse.button === Qt.RightButton) {
                // Right-click chain for fullscreen exit
                root.handleFullscreenRightClick();
                mouse.accepted = true;
            }
        }
    }

    // Thumbnail strip
    ThumbStrip {
        id: thumbStrip
        stripSide: sessionState.stripSide()  // 0=Bottom, 1=Left
        currentIndex: viewerController.currentIndex
        visible: root.thumbStripVisible

        model: galleryModel

        onJumpToIndex: (index) => {
            viewerController.open(index);
        }
    }

    // Keyboard navigation
    Keys.onLeftPressed: {
        viewerController.prev();
        event.accepted = true;
    }
    Keys.onRightPressed: {
        viewerController.next();
        event.accepted = true;
    }
    Keys.onSpacePressed: {
        // Space: play/pause animated image
        if (animController !== null && animController !== undefined) {
            animController.togglePlayPause();
            event.accepted = true;
        }
    }
    Keys.onPressed: {
        if (event.key === Qt.Key_F) {
            // Task 3.2: F cycles zoom modes (Fit ↔ FillScroll), Shift+F toggles fullscreen
            if (event.modifiers & Qt.ShiftModifier) {
                // Shift+F: toggle fullscreen
                if (root.isFullscreen) {
                    root.exitFullscreen();
                } else {
                    root.enterFullscreen();
                }
                event.accepted = true;
            } else {
                // F: cycle zoom modes
                root.cycleZoomMode();
                event.accepted = true;
            }
        } else if (event.key === Qt.Key_1 || event.key === Qt.Key_Exclam) {
            // 1: reset to fit
            root.resetFit();
            event.accepted = true;
        } else if (event.key === Qt.Key_T) {
            // Toggle strip visibility
            root.thumbStripVisible = !root.thumbStripVisible;
            event.accepted = true;
        } else if (event.key === Qt.Key_P) {
            // P: toggle slideshow
            root.toggleSlideshow();
            event.accepted = true;
        } else if (event.key === Qt.Key_BracketLeft) {
            // [: decrease slideshow dwell
            root.adjustSlideshowDwell(-1.0);
            event.accepted = true;
        } else if (event.key === Qt.Key_BracketRight) {
            // ]: increase slideshow dwell
            root.adjustSlideshowDwell(1.0);
            event.accepted = true;
        }
    }
    Keys.onEscapePressed: {
        // Esc: exit slideshow first, then viewer
        if (root.slideshowRunning) {
            root.toggleSlideshow();
        } else {
            // Pop back to gallery view - parent is StackView when pushed
            if (parent && typeof parent.pop === 'function') {
                parent.pop();
            }
        }
        event.accepted = true;
    }

    // Thumbnail strip visibility controlled by thumbStripVisible and fullscreen state
    Binding {
        target: thumbStrip
        property: "visible"
        value: root.thumbStripVisible && !root.isFullscreen
    }

    // Header overlay: "name · N/M · zoom%" (shown at top in fullscreen, or as name label normally)
    Text {
        id: headerText
        anchors {
            top: parent.top
            left: parent.left
            margins: 12
        }
        text: root.buildHeaderText()
        color: themePalette.text
        font.pixelSize: 12
        elide: Text.ElideRight
        width: Math.min(400, parent.width - 24)
        visible: root.isFullscreen || !root.isFullscreen  // always visible in normal mode; hide in fullscreen if no text
    }

    // Slideshow HUD: dwell time indicator and status
    Rectangle {
        anchors {
            bottom: root.thumbStripVisible && root.stripSide === 0 ? thumbStrip.top : parent.bottom
            bottom.margins: 20
            horizontalCenter: parent.horizontalCenter
        }
        width: 200
        height: 60
        color: themePalette.bg
        opacity: 0.8
        radius: 8
        visible: root.slideshowRunning

        Column {
            anchors.centerIn: parent
            spacing: 4
            width: parent.width - 20

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Slideshow"
                color: themePalette.text
                font.pixelSize: 14
                font.bold: true
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Dwell: " + root.slideshowDwell.toFixed(1) + "s"
                color: themePalette.textDim
                font.pixelSize: 12
            }

            ProgressBar {
                width: parent.width - 20
                anchors.horizontalCenter: parent.horizontalCenter
                from: 0
                to: root.slideshowDwell
                value: root.slideshowElapsed
                height: 4
            }
        }
    }
}
