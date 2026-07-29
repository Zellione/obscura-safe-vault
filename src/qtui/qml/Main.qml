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
            VideoScreen {
            }
        }
    }
}
