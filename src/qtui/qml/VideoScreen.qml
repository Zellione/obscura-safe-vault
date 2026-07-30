import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Osv 1.0

Rectangle {
    id: root
    color: themePalette.imgBg
    focus: true

    property real scale: 1.0
    property real panX: 0
    property real panY: 0
    property real fitScale: 1.0

    // Help groups for F1 help popup
    property var helpGroups: [
        {
            title: "Video Player",
            entries: [
                { keys: "Space", description: "Play/pause" },
                { keys: "J/L", description: "Seek ±5 seconds" },
                { keys: "R", description: "Toggle loop" },
                { keys: "[/]", description: "Volume ±5%" },
                { keys: ",/.", description: "Frame-step forward/backward" },
                { keys: "M", description: "Mute" },
                { keys: "+/-", description: "Volume ±5%" },
                { keys: "Scroll", description: "Zoom" },
                { keys: "Esc", description: "Exit video" }
            ]
        }
    ]

    // Bind playback engine to this video item
    Component.onCompleted: {
        if (playbackEngine) {
            playbackEngine.setFrameItem(videoItem);
            // Auto-play
            playbackEngine.play();
            updateFitScale();
        }
    }

    Component.onDestruction: {
        if (playbackEngine) {
            playbackEngine.stop();
            playbackEngine.setFrameItem(null);
        }
    }

    function updateFitScale() {
        if (videoItem.sourceSize.width > 0 && videoItem.sourceSize.height > 0) {
            fitScale = Math.min(
                width / videoItem.sourceSize.width,
                height / videoItem.sourceSize.height
            );
            if (scale < fitScale) {
                scale = fitScale;
                panX = 0;
                panY = 0;
            }
        }
    }

    function resetFit() {
        scale = fitScale;
        panX = 0;
        panY = 0;
    }

    onWidthChanged: updateFitScale()
    onHeightChanged: updateFitScale()

    Connections {
        target: playbackEngine
        function onDurationChanged() {
            updateFitScale();
        }
    }

    // Video frame item: centered, letterboxed
    VideoFrameItem {
        id: videoItem
        width: sourceSize.width * scale
        height: sourceSize.height * scale
        x: root.width / 2 - width / 2 + panX
        y: root.height / 2 - height / 2 + panY
        visible: playbackEngine && playbackEngine.duration > 0

        onSourceSizeChanged: {
            root.updateFitScale();
        }
    }

    // Loading indicator
    Text {
        anchors.centerIn: parent
        text: "Loading video..."
        color: themePalette.textDim
        font.pixelSize: 16
        visible: !videoItem.visible
    }

    // Playback controls overlay (bottom)
    Rectangle {
        id: controlsOverlay
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            margins: 0
        }
        color: Qt.rgba(0, 0, 0, 0.6)
        visible: playbackEngine && playbackEngine.duration > 0

        Column {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                margins: 0
            }
            spacing: 0

            // Seek bar
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 8
                color: Qt.rgba(0.3, 0.3, 0.3, 0.8)

                Rectangle {
                    height: parent.height
                    width: playbackEngine.duration > 0 ?
                           (playbackEngine.position / playbackEngine.duration) * parent.width : 0
                    color: themePalette.accent
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: (mouse) => {
                        if (playbackEngine.duration > 0) {
                            const pos = (mouse.x / width) * playbackEngine.duration;
                            playbackEngine.seekBy(pos - playbackEngine.position);
                        }
                    }
                }
            }

            // Controls row
            Row {
                anchors {
                    left: parent.left
                    right: parent.right
                    margins: 12
                }
                height: 60
                spacing: 12
                padding: 8

                // Play/pause button
                Button {
                    text: playbackEngine.playing ? "Pause" : "Play"
                    onClicked: {
                        if (playbackEngine.playing) {
                            playbackEngine.pause();
                        } else {
                            playbackEngine.play();
                        }
                    }
                    palette.buttonText: themePalette.text
                }

                // Position display
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: (playbackEngine.clockText ?? "0:00") + " / " +
                          (function() {
                            const d = playbackEngine.duration;
                            const mins = Math.floor(d / 60);
                            const secs = Math.floor(d % 60);
                            return (mins < 10 ? "0" : "") + mins + ":" +
                                   (secs < 10 ? "0" : "") + secs;
                          })()
                    color: themePalette.text
                    font.pixelSize: 12
                }

                // Spacer
                Item { Layout.fillWidth: true }

                // Loop indicator
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 40
                    height: 24
                    color: playbackEngine.loopEnabled ? themePalette.accent : Qt.rgba(0.4, 0.4, 0.4, 0.5)
                    radius: 4

                    Text {
                        anchors.centerIn: parent
                        text: "R"
                        color: playbackEngine.loopEnabled ? themePalette.imgBg : themePalette.textDim
                        font.pixelSize: 12
                        font.bold: true
                    }
                }
            }
        }
    }

    // Keyboard controls
    Keys.onSpacePressed: {
        if (playbackEngine) {
            playbackEngine.playing ? playbackEngine.pause() : playbackEngine.play();
        }
        event.accepted = true;
    }

    Keys.onPressed: {
        if (!playbackEngine) return;

        if (event.key === Qt.Key_J) {
            playbackEngine.seekBy(-5.0);
            event.accepted = true;
        } else if (event.key === Qt.Key_L) {
            playbackEngine.seekBy(5.0);
            event.accepted = true;
        } else if (event.key === Qt.Key_F) {
            root.resetFit();
            event.accepted = true;
        } else if (event.key === Qt.Key_M) {
            playbackEngine.toggleMute();
            event.accepted = true;
        } else if (event.key === Qt.Key_R) {
            // R: toggle loop mode
            playbackEngine.toggleLoop();
            event.accepted = true;
        } else if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
            // + or = key (same key on US keyboard)
            playbackEngine.setVolume(playbackEngine.volume + 0.05);
            event.accepted = true;
        } else if (event.key === Qt.Key_Minus) {
            playbackEngine.setVolume(playbackEngine.volume - 0.05);
            event.accepted = true;
        } else if (event.key === Qt.Key_BracketLeft) {
            // [: volume -5%
            playbackEngine.setVolume(playbackEngine.volume - 0.05);
            event.accepted = true;
        } else if (event.key === Qt.Key_BracketRight) {
            // ]: volume +5%
            playbackEngine.setVolume(playbackEngine.volume + 0.05);
            event.accepted = true;
        } else if (event.key === Qt.Key_Comma) {
            // ,: frame-step backward (when paused)
            if (!playbackEngine.playing) {
                playbackEngine.stepFrame(-1);
                event.accepted = true;
            }
        } else if (event.key === Qt.Key_Period) {
            // .: frame-step forward (when paused)
            if (!playbackEngine.playing) {
                playbackEngine.stepFrame(1);
                event.accepted = true;
            }
        }
    }

    Keys.onEscapePressed: {
        if (playbackEngine) {
            playbackEngine.stop();
        }
        if (parent && typeof parent.pop === 'function') {
            parent.pop();
        }
        event.accepted = true;
    }

    // Wheel zoom (like viewer)
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse
        onWheel: {
            const oldScale = root.scale;
            let factor = 1.15;
            if (event.angleDelta.y < 0) {
                factor = 1 / factor;
            }

            const newScale = Math.max(root.fitScale, Math.min(8, oldScale * factor));
            if (newScale === oldScale) return;

            const cursorX = event.position.x;
            const cursorY = event.position.y;

            const imageCenterX = videoItem.width / 2;
            const imageCenterY = videoItem.height / 2;

            const cursorOffsetX = cursorX - root.width / 2;
            const cursorOffsetY = cursorY - root.height / 2;

            const cursorToImageX = (cursorOffsetX - root.panX) / oldScale;
            const cursorToImageY = (cursorOffsetY - root.panY) / oldScale;

            root.scale = newScale;

            root.panX = cursorOffsetX - (cursorToImageX * newScale);
            root.panY = cursorOffsetY - (cursorToImageY * newScale);
        }
    }

    // Drag to pan
    DragHandler {
        acceptedButtons: Qt.LeftButton
        onTranslationChanged: {
            root.panX += translation.x;
            root.panY += translation.y;
        }
    }
}
