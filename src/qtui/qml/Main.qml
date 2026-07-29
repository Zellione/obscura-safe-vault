import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Osv 1.0

Window {
    id: root
    width: 1280
    height: 800
    visible: true
    color: themePalette.bg
    title: "osv-qt (experiment)"

    property int currentThemeIndex: 0

    RenameDialog {
        id: renameDialog
    }

    StackView {
        id: stack
        anchors.fill: parent

        initialItem: unlockScreenComponent

        Connections {
            target: unlockController
            function onUnlockedChanged() {
                if (unlockController.unlocked) {
                    stack.replace(unlockedPageComponent)
                } else {
                    stack.replace(unlockScreenComponent)
                }
            }
        }

        Connections {
            target: galleryModel
            function onOpenViewer(row) {
                stack.push(viewerScreenComponent);
                viewerController.open(row);
            }
            function onOpenVideo(row) {
                stack.push(videoScreenComponent);
                playbackEngine.open(galleryModel.data(galleryModel.index(row, 0), galleryModel.roleNames()['nodeKey']));
            }
        }

        // Inline unlock screen (was UnlockScreen.qml)
        Component {
            id: unlockScreenComponent
            Rectangle {
                id: unlockRoot
                color: themePalette.bg

                FileDialog {
                    id: fileDialog
                    fileMode: FileDialog.OpenFile
                    nameFilters: ["Vault files (*.osv)", "All files (*)"]
                    onAccepted: {
                        unlockController.openVault(fileDialog.selectedFile)
                        passwordField.focus = true
                    }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 16
                    width: 300

                    // Vault file picker
                    Rectangle {
                        width: parent.width
                        height: 44
                        color: themePalette.surface
                        border.color: themePalette.border
                        border.width: 1
                        radius: 4

                        MouseArea {
                            anchors.fill: parent
                            onClicked: fileDialog.open()
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "Open Vault"
                            color: themePalette.text
                            font.pixelSize: 14
                        }
                    }

                    // Password field
                    SecureTextField {
                        id: passwordField
                        width: parent.width
                        height: 44
                        focus: false

                        Rectangle {
                            anchors.fill: parent
                            color: themePalette.surface
                            border.color: themePalette.border
                            border.width: 1
                            radius: 4
                            z: -1
                        }

                        onAccepted: {
                            unlockController.unlock(passwordField)
                        }
                    }

                    // Unlock button
                    Rectangle {
                        width: parent.width
                        height: 44
                        color: themePalette.surface
                        border.color: themePalette.border
                        border.width: 1
                        radius: 4

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                unlockController.unlock(passwordField)
                            }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "Unlock"
                            color: themePalette.text
                            font.pixelSize: 14
                        }
                    }

                    // Error message
                    Text {
                        width: parent.width
                        text: unlockController.errorText
                        color: themePalette.danger
                        font.pixelSize: 12
                        wrapMode: Text.Wrap
                        visible: text.length > 0
                    }
                }
            }
        }

        // Unlocked page with gallery grid
        Component {
            id: unlockedPageComponent
            Rectangle {
                id: galleryRoot
                color: themePalette.bg
                anchors.fill: parent

                // Gallery grid view: displays galleries and media from galleryModel.
                // Galleries shown as folder glyphs, media as thumbnails.
                // Arrow keys navigate, Enter opens, Esc up/back, F2 rename, T cycle theme.
                GridView {
                    id: grid
                    anchors.fill: parent
                    cellWidth: 176
                    cellHeight: 200
                    model: galleryModel
                    focus: true

                    delegate: Item {
                        width: grid.cellWidth
                        height: grid.cellHeight

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 6
                            color: GridView.isCurrentItem ? themePalette.surfaceHi : themePalette.surface
                            radius: 6

                            // Thumbnail image for media
                            SecureImageItem {
                                visible: !model.isGallery
                                anchors {
                                    fill: parent
                                    margins: 6
                                    bottomMargin: 26
                                }
                                nodeKey: model.nodeKey
                            }

                            // Folder glyph for galleries
                            Text {
                                visible: model.isGallery
                                anchors.centerIn: parent
                                text: "📁"
                                font.pixelSize: 48
                            }

                            // Name label
                            Text {
                                anchors {
                                    bottom: parent.bottom
                                    horizontalCenter: parent.horizontalCenter
                                    margins: 4
                                }
                                width: parent.width - 12
                                elide: Text.ElideMiddle
                                text: model.name
                                color: themePalette.text
                                horizontalAlignment: Text.AlignHCenter
                                font.pixelSize: 12
                            }
                        }

                        // Single-tap: select item
                        TapHandler {
                            onTapped: grid.currentIndex = index
                        }

                        // Double-tap: activate (open gallery or image viewer)
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            gesturePolicy: TapHandler.DragThreshold
                            onDoubleTapped: galleryModel.activate(index)
                        }
                    }

                    // Keyboard navigation
                    Keys.onReturnPressed: {
                        galleryModel.activate(grid.currentIndex)
                    }
                    Keys.onEscapePressed: {
                        galleryModel.upOneLevel()
                    }
                    Keys.onPressed: {
                        if (event.key === Qt.Key_F2) {
                            // Rename current item
                            if (grid.currentIndex >= 0 && grid.currentIndex < galleryModel.rowCount()) {
                                const nodeName = galleryModel.data(galleryModel.index(grid.currentIndex), galleryModel.roleNames()['name']);
                                renameDialog.originalName = nodeName;
                                renameDialog.targetRow = grid.currentIndex;
                                renameDialog.open();
                            }
                            event.accepted = true;
                        } else if (event.key === Qt.Key_T) {
                            // Cycle to next theme (0->1->2->3->0)
                            // We track theme state by trying incrementally
                            root.currentThemeIndex = (root.currentThemeIndex + 1) % 4;
                            themePalette.setThemeIndex(root.currentThemeIndex);
                            event.accepted = true;
                        }
                    }
                }
            }
        }

        // Image viewer screen
        Component {
            id: viewerScreenComponent
            ViewerScreen {
            }
        }

        // Video player screen
        Component {
            id: videoScreenComponent
            Rectangle {
                    id: videoRoot
                    color: "#000000"
                    focus: true
                    property real videoScale: 1.0
                    property real videoPanX: 0
                    property real videoPanY: 0
                    property real videoFitScale: 1.0

                    function updateFitScale() {
                        if (videoItem && videoItem.sourceSize && videoItem.sourceSize.width > 0) {
                            videoFitScale = Math.min(
                                width / videoItem.sourceSize.width,
                                height / videoItem.sourceSize.height
                            );
                            if (videoScale < videoFitScale) {
                                videoScale = videoFitScale;
                                videoPanX = 0;
                                videoPanY = 0;
                            }
                        }
                    }

                    function resetFit() {
                        videoScale = videoFitScale;
                        videoPanX = 0;
                        videoPanY = 0;
                    }

                    Component.onCompleted: {
                        if (playbackEngine && videoItem) {
                            playbackEngine.setFrameItem(videoItem);
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

                    onWidthChanged: updateFitScale()
                    onHeightChanged: updateFitScale()

                    VideoFrameItem {
                        id: videoItem
                        width: sourceSize.width * videoScale
                        height: sourceSize.height * videoScale
                        x: videoRoot.width / 2 - width / 2 + videoPanX
                        y: videoRoot.height / 2 - height / 2 + videoPanY
                        visible: playbackEngine && playbackEngine.duration > 0
                        onSourceSizeChanged: videoRoot.updateFitScale()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "Loading video..."
                        color: themePalette.textDim ?? "#aaaaaa"
                        font.pixelSize: 16
                        visible: !videoItem.visible
                    }

                    Rectangle {
                        anchors {
                            bottom: parent.bottom
                            left: parent.left
                            right: parent.right
                            margins: 0
                        }
                        height: 60
                        color: Qt.rgba(0, 0, 0, 0.6)
                        visible: playbackEngine && playbackEngine.duration > 0

                        Row {
                            anchors {
                                verticalCenter: parent.verticalCenter
                                left: parent.left
                                margins: 12
                            }
                            spacing: 12

                            Button {
                                text: playbackEngine.playing ? "Pause" : "Play"
                                onClicked: playbackEngine.playing ? playbackEngine.pause() : playbackEngine.play()
                                palette.buttonText: themePalette.text ?? "white"
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: (playbackEngine.clockText ?? "0:00") + " / " + (function() {
                                    const d = playbackEngine.duration || 0;
                                    const mins = Math.floor(d / 60);
                                    const secs = Math.floor(d % 60);
                                    return (mins < 10 ? "0" : "") + mins + ":" + (secs < 10 ? "0" : "") + secs;
                                })()
                                color: themePalette.text ?? "white"
                                font.pixelSize: 12
                            }
                        }
                    }

                    Keys.onSpacePressed: {
                        if (playbackEngine) playbackEngine.playing ? playbackEngine.pause() : playbackEngine.play();
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
                            videoRoot.resetFit();
                            event.accepted = true;
                        }
                    }

                    Keys.onEscapePressed: {
                        if (playbackEngine) playbackEngine.stop();
                        if (parent && typeof parent.pop === 'function') parent.pop();
                        event.accepted = true;
                    }
                }
            }
        }
    }
}
