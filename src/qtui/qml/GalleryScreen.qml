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
                { keys: "Shift+S", description: "Cycle sort order" },
                { keys: "L", description: "Cycle view density (grid/list)" }
            ]
        }
    ]

    // Signal for back navigation (Main.qml wires to galleryModel.upOneLevel)
    signal back()

    // View mode helpers (synchronized with sessionState)
    // GalleryView enum: 0=List, 1=GridS, 2=GridM, 3=GridL, 4=GridXL
    readonly property var cellSizes: [0, 128, 188, 248, 320]  // index 0 (List) is ignored
    readonly property int currentViewMode: sessionState.viewDensity()

    function nextViewMode() {
        // Cycle: 0(List) -> 1(GridS) -> 2(GridM) -> 3(GridL) -> 4(GridXL) -> 0(List)
        sessionState.setViewDensity((currentViewMode + 1) % 5)
    }

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
                text: {
                    let path = galleryModel.currentPath
                    // TODO: Task 2.1 - add sort key label when non-Default
                    // when (galleryModel.sortKey) implemented
                    return path
                }
                color: themePalette.textDim
                font.pixelSize: 13
                elide: Text.ElideLeft
            }
        }
    }

    // Gallery grid view: displays galleries and media from galleryModel.
    // Galleries shown as folder glyphs, media as thumbnails.
    // Arrow keys navigate, Enter opens, Esc up/back, Shift+S sort, L view mode.
    GridView {
        id: grid
        anchors {
            top: galleryHeader.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        cellWidth: currentViewMode === 0 ? width : cellSizes[currentViewMode] + 12
        cellHeight: currentViewMode === 0 ? 60 : cellSizes[currentViewMode] + 12
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
        // Note: F2 is now global (opens settings overlay from Main.qml)
        Keys.onPressed: (event) => {
            if (event.text === "S" && event.modifiers & Qt.ShiftModifier) {
                // Shift+S: cycle sort order
                galleryModel.nextSort()
                event.accepted = true
            } else if (event.text === "L" || event.text === "l") {
                // L: cycle view density
                nextViewMode()
                event.accepted = true
            }
        }
    }
}
