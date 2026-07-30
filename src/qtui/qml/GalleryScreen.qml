import QtQuick
import QtQuick.Controls
import Osv 1.0

Rectangle {
    id: galleryRoot
    color: themePalette.bg
    anchors.fill: parent

    // Dialog for renaming items (passed from Main.qml)
    property var renameDialog: null

    // Help groups for F1 help popup
    property var helpGroups: [
        {
            title: "Gallery Navigation",
            entries: [
                { keys: "Arrow Keys", description: "Navigate gallery" },
                { keys: "Enter", description: "Open image/video or enter gallery" },
                { keys: "Esc", description: "Go up one level" },
                { keys: "F2", description: "Rename" }
            ]
        }
    ]

    // Signal to request theme cycling (Main.qml handles the state)
    signal themeCycleRequested()

    // Signal for back navigation (Main.qml wires to galleryModel.upOneLevel)
    signal back()

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
                        if (galleryModel.currentPath !== "/") {
                            galleryModel.upOneLevel()
                            grid.forceActiveFocus()
                            galleryRoot.back()
                        }
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
            galleryRoot.back()
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
                // Signal to Main.qml to handle theme cycling
                galleryRoot.themeCycleRequested();
                event.accepted = true;
            }
        }
    }
}
