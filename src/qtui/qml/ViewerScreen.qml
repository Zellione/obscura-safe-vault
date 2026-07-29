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

    // Calculate fit-to-viewport scale
    function updateFitScale() {
        if (imageItem.sourceSize.width > 0 && imageItem.sourceSize.height > 0) {
            fitScale = Math.min(
                width / imageItem.sourceSize.width,
                height / imageItem.sourceSize.height
            );
            // Clamp zoom to at least fitScale
            if (zoom < fitScale) {
                zoom = fitScale;
                resetPan();
            }
        }
    }

    function resetFit() {
        zoom = fitScale;
        resetPan();
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

    // Bind to controller
    Component.onCompleted: {
        viewerController.bindItem(imageItem);
        updateFitScale();
    }

    // Unbind from controller on destruction (prevents use-after-free if image load is in flight)
    Component.onDestruction: {
        viewerController.bindItem(null);
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

        onSourceSizeChanged: {
            root.updateFitScale();
        }
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

    // Keyboard navigation
    Keys.onLeftPressed: {
        viewerController.prev();
        event.accepted = true;
    }
    Keys.onRightPressed: {
        viewerController.next();
        event.accepted = true;
    }
    Keys.onPressed: {
        if (event.key === Qt.Key_F) {
            root.resetFit();
            event.accepted = true;
        }
    }
    Keys.onEscapePressed: {
        // Pop back to gallery view - parent is StackView when pushed
        if (parent && typeof parent.pop === 'function') {
            parent.pop();
        }
        event.accepted = true;
    }

    // Image name overlay (top-left corner)
    Text {
        anchors {
            top: parent.top
            left: parent.left
            margins: 12
        }
        text: viewerController.imageName
        color: themePalette.text
        font.pixelSize: 12
        elide: Text.ElideRight
        width: Math.min(300, parent.width - 24)
    }
}
