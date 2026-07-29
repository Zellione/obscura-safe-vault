import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Osv 1.0

Window {
    id: root
    width: 1280
    height: 800
    visible: true
    color: "#14161a"
    title: "osv-qt (experiment)"

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
        }

        // Inline unlock screen (was UnlockScreen.qml)
        Component {
            id: unlockScreenComponent
            Rectangle {
                id: unlockRoot
                color: "#14161a"

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
                        color: "#2a2d33"
                        border.color: "#3f4349"
                        border.width: 1
                        radius: 4

                        MouseArea {
                            anchors.fill: parent
                            onClicked: fileDialog.open()
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "Open Vault"
                            color: "#c8ccd4"
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
                            color: "#2a2d33"
                            border.color: "#3f4349"
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
                        color: "#2a2d33"
                        border.color: "#3f4349"
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
                            color: "#c8ccd4"
                            font.pixelSize: 14
                        }
                    }

                    // Error message
                    Text {
                        width: parent.width
                        text: unlockController.errorText
                        color: "#ff6b6b"
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
                color: "#14161a"
                anchors.fill: parent

                // Gallery grid view: displays galleries and media from galleryModel.
                // Galleries shown as folder glyphs, media as thumbnails.
                // Arrow keys navigate, Enter opens, Esc up/back.
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
                            color: GridView.isCurrentItem ? "#2a3140" : "#1b1f27"
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
                                color: "#c8ccd4"
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
                }
            }
        }

        // Image viewer screen
        Component {
            id: viewerScreenComponent
            ViewerScreen {
            }
        }
    }
}
