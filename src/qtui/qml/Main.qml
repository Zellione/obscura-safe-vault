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
                var nodeKey = galleryModel.nodeKeyAt(row);
                stack.push(videoScreenComponent);
                playbackEngine.open(nodeKey);
            }
        }

        // Inline unlock screen (was UnlockScreen.qml)
        Component {
            id: unlockScreenComponent
            Rectangle {
                id: unlockRoot
                color: themePalette.bg

                // Row of the known-vault list currently opened (-1: none / file dialog)
                property int selectedVaultRow: -1

                // Pick up registry/folder changes each time the screen appears
                Component.onCompleted: vaultListModel.refresh()

                FileDialog {
                    id: fileDialog
                    fileMode: FileDialog.OpenFile
                    nameFilters: ["Vault files (*.osv)", "All files (*)"]
                    onAccepted: {
                        if (unlockController.openVault(fileDialog.selectedFile)) {
                            unlockRoot.selectedVaultRow = -1
                        }
                        passwordField.forceActiveFocus()
                    }
                }

                Column {
                    anchors.centerIn: parent
                    spacing: 16
                    width: 420

                    // Known vaults: registry entries + *.osv in the per-user data
                    // folder. That folder hides under ~/.local/share (hidden dir),
                    // which the native file dialog won't show — this list makes
                    // those vaults directly clickable.
                    Rectangle {
                        width: parent.width
                        height: Math.min(vaultList.contentHeight + 2, 264)
                        visible: vaultListModel.count > 0
                        color: "transparent"
                        border.color: themePalette.border
                        border.width: 1
                        radius: 4
                        clip: true

                        ListView {
                            id: vaultList
                            anchors.fill: parent
                            anchors.margins: 1
                            model: vaultListModel
                            spacing: 1
                            boundsBehavior: Flickable.StopAtBounds

                            delegate: Rectangle {
                                width: vaultList.width
                                height: 52
                                color: unlockRoot.selectedVaultRow === index
                                       ? themePalette.surfaceHi : themePalette.surface

                                Column {
                                    anchors {
                                        verticalCenter: parent.verticalCenter
                                        left: parent.left
                                        right: parent.right
                                        leftMargin: 12
                                        rightMargin: 12
                                    }
                                    spacing: 2

                                    Text {
                                        width: parent.width
                                        text: model.name
                                        color: themePalette.text
                                        font.pixelSize: 14
                                        elide: Text.ElideMiddle
                                    }
                                    Text {
                                        width: parent.width
                                        text: model.dir
                                        color: themePalette.textDim
                                        font.pixelSize: 11
                                        elide: Text.ElideMiddle
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        if (unlockController.openVault(vaultListModel.fileUrlAt(index))) {
                                            unlockRoot.selectedVaultRow = index
                                            passwordField.forceActiveFocus()
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Vault file picker (for vaults not in the list)
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
                            text: vaultListModel.count > 0 ? "Open Other Vault…" : "Open Vault"
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

                // `focus: true` only grants scope focus inside the StackView;
                // keys (Esc = up, arrows) need ACTIVE focus on the grid — force
                // it when the page appears and again when the viewer/video
                // screen pops back to it.
                Component.onCompleted: grid.forceActiveFocus()
                StackView.onActivated: grid.forceActiveFocus()

                // Header: current gallery path + mouse affordance to go up.
                Rectangle {
                    id: galleryHeader
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    height: 44
                    color: themePalette.surface

                    Row {
                        anchors {
                            verticalCenter: parent.verticalCenter
                            left: parent.left
                            leftMargin: 8
                        }
                        spacing: 10

                        Rectangle {
                            width: 72
                            height: 30
                            radius: 4
                            visible: galleryModel.currentPath !== "/"
                            color: upMouse.pressed ? themePalette.surfaceHi : themePalette.bg
                            border.color: themePalette.border
                            border.width: 1
                            anchors.verticalCenter: parent.verticalCenter

                            MouseArea {
                                id: upMouse
                                anchors.fill: parent
                                onClicked: {
                                    galleryModel.upOneLevel()
                                    grid.forceActiveFocus()
                                }
                            }

                            Text {
                                anchors.centerIn: parent
                                text: "⬆ Up"
                                color: themePalette.text
                                font.pixelSize: 13
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: galleryModel.currentPath
                            color: themePalette.textDim
                            font.pixelSize: 13
                            elide: Text.ElideLeft
                        }
                    }
                }

                // Gallery grid view: displays galleries and media from galleryModel.
                // Galleries shown as folder glyphs, media as thumbnails.
                // Arrow keys navigate, Enter opens, Esc up/back, F2 rename, T cycle theme.
                GridView {
                    id: grid
                    anchors {
                        top: galleryHeader.bottom
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }
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

                        // Single-tap: select item (and give the grid key focus,
                        // so Esc/arrows work after any mouse interaction)
                        TapHandler {
                            onTapped: {
                                grid.currentIndex = index
                                grid.forceActiveFocus()
                            }
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
