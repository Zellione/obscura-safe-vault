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

    // Global F1/F2/Shift+T/Shift+G/Shift+/ key handlers
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_F1) {
            helpPopup.toggle();
            event.accepted = true;
        } else if (event.key === Qt.Key_F2) {
            settingsOverlay.toggle();
            event.accepted = true;
        } else if (event.key === Qt.Key_T && (event.modifiers & Qt.ShiftModifier)) {
            // Shift+T: open tag overview screen
            stack.push(tagOverviewScreenComponent);
            event.accepted = true;
        } else if (event.key === Qt.Key_G && (event.modifiers & Qt.ShiftModifier)) {
            // Shift+G: import tags from file to current gallery
            tagListImportFileDialog.open();
            event.accepted = true;
        } else if (event.key === Qt.Key_Slash && (event.modifiers & Qt.ShiftModifier)) {
            // Shift+/: open advanced search screen
            stack.push(advancedSearchScreenComponent);
            event.accepted = true;
        }
    }

    RenameDialog {
        id: renameDialog
    }

    HelpPopup {
        id: helpPopup
        helpModel: helpModel

        // Restore focus to the active screen when help closes
        onClosed: {
            if (stack.currentItem) {
                stack.currentItem.forceActiveFocus();
            }
        }
    }

    SettingsOverlay {
        id: settingsOverlay
        settingsController: settingsController

        // Restore focus to the active screen when settings closes
        onClosed: {
            if (stack.currentItem) {
                stack.currentItem.forceActiveFocus();
            }
        }
    }

    // FileDialog for importing tag lists
    FileDialog {
        id: tagListImportFileDialog
        title: "Import Tags from File"
        nameFilters: ["Text files (*.txt)", "All files (*)"]
        onAccepted: {
            // Import tags to current gallery path
            const currentPath = galleryModel.currentPath;
            const result = tagListImportController.importTagsFromFile(currentPath, selectedFile.toString());
            if (result >= 0) {
                statusController.set(0, `Imported ${result} tags`);
            } else {
                statusController.set(2, tagListImportController.errorMessage());
            }
            if (stack.currentItem) {
                stack.currentItem.forceActiveFocus();
            }
        }
        onRejected: {
            if (stack.currentItem) {
                stack.currentItem.forceActiveFocus();
            }
        }
    }

    FooterBar {
        id: footerBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
    }

    StackView {
        id: stack
        anchors {
            fill: parent
            bottom: footerBar.top
        }

        initialItem: unlockScreenComponent

        // When current item changes, update help popup with new screen's groups
        onCurrentItemChanged: {
            if (currentItem && currentItem.helpGroups !== undefined) {
                helpModel.setScreenGroups(currentItem.helpGroups);
            }
        }

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
                var nodeKey = galleryModel.nodeKeyAt(row);
                stack.push(videoScreenComponent);
                playbackEngine.open(nodeKey);
            }
        }

        // Unlock screen
        Component {
            id: unlockScreenComponent
            UnlockScreen {
            }
        }

        // Gallery screen
        Component {
            id: unlockedPageComponent
            GalleryScreen {
                renameDialog: renameDialog
                onBack: {
                    // Navigation contract satisfied; upOneLevel() already called by GalleryScreen.
                    // WS1's vault-manager route will consume this signal for deeper back behaviors.
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

                    // Help groups for F1 help popup
                    property var helpGroups: [
                        {
                            title: "Video Player",
                            entries: [
                                { keys: "Space", description: "Play/pause" },
                                { keys: "J/L", description: "Seek -5s / +5s" },
                                { keys: "F", description: "Fit to window" },
                                { keys: "Esc", description: "Close player" }
                            ]
                        }
                    ]

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
                        color: themePalette.textDim
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
                                palette.buttonText: themePalette.text
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: (playbackEngine.clockText ?? "0:00") + " / " + (function() {
                                    const d = playbackEngine.duration || 0;
                                    const mins = Math.floor(d / 60);
                                    const secs = Math.floor(d % 60);
                                    return (mins < 10 ? "0" : "") + mins + ":" + (secs < 10 ? "0" : "") + secs;
                                })()
                                color: themePalette.text
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

        // Tag overview screen
        Component {
            id: tagOverviewScreenComponent
            TagOverviewScreen {
                onBack: {
                    stack.pop();
                }
            }
        }

        // Advanced search screen
        Component {
            id: advancedSearchScreenComponent
            AdvancedSearchScreen {
                onBack: {
                    stack.pop();
                }
            }
        }
}
