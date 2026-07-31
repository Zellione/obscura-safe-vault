import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import Osv 1.0

Rectangle {
    id: root
    color: themePalette.bg

    // themePalette / favoritesController come from engine context properties
    // (required-property shadowing breaks self-named bindings — T3.1 W5).

    signal back()
    signal openGallery(nodePath: string)
    // T3.1 W5: shell pushes the viewer screen then calls viewerController.openAlbum
    signal openAlbum(var nodeKeys, int startIndex)

    // Help groups
    property list<var> helpGroups: [
        { title: "Navigation", entries: [
            { keys: "Tab", description: "Toggle: galleries ↔ images" },
            { keys: "Up/Down", description: "Navigate favorites" },
            { keys: "Enter/Return", description: "Open selected" },
            { keys: "Esc", description: "Return to gallery" }
        ]}
    ]

    // Track which face is active: 0 = galleries, 1 = images
    property int activeFace: 0
    property var currentFavorites: []

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: themePalette.surfaceHi
            border.color: themePalette.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 16

                Text {
                    text: activeFace === 0 ? "Favorite Galleries" : "Favorite Images"
                    color: themePalette.text
                    font.pixelSize: 14
                    font.bold: true
                    Layout.fillWidth: true
                }

                Text {
                    text: "(Tab to switch, Esc to exit)"
                    color: themePalette.textDim
                    font.pixelSize: 12
                }
            }
        }

        // Grid or list view for favorites
        GridView {
            id: gridView
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            cellWidth: 128
            cellHeight: 128
            interactive: true
            clip: true

            model: currentFavorites.length

            delegate: Rectangle {
                // model is a count — resolve the row via index (modelData is just the int)
                readonly property var favItem: currentFavorites[index]
                width: gridView.cellWidth - 4
                height: gridView.cellHeight - 4
                color: themePalette.surface
                border.color: (gridView.currentIndex === index)
                    ? themePalette.accent
                    : themePalette.border
                border.width: (gridView.currentIndex === index) ? 3 : 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: themePalette.imgBg

                        // Thumbnail image for media
                        SecureImageItem {
                            visible: !favItem.is_gallery
                            anchors.fill: parent
                            nodeKey: favItem.nodeKey
                        }

                        // Folder glyph for galleries
                        Text {
                            visible: favItem.is_gallery
                            anchors.centerIn: parent
                            text: "📁"
                            font.pixelSize: 36
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: favItem.name
                        color: themePalette.text
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        gridView.currentIndex = index;
                    }
                    onDoubleClicked: {
                        gridView.currentIndex = index;
                        if (favItem.is_gallery) {
                            root.openGallery(favItem.path);
                        } else {
                            // Open in collection mode (WS3 contract)
                            root.openAlbum(
                                currentFavorites.map(item => item.nodeKey || 0),
                                index
                            );
                        }
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }

        // Empty state
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: themePalette.bg
            visible: currentFavorites.length === 0

            Text {
                anchors.centerIn: parent
                text: activeFace === 0
                    ? "No favorite galleries\n(B to toggle, Shift+F to view)"
                    : "No favorite images\n(B to toggle, Shift+F to view)"
                color: themePalette.textDim
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    onActiveFaceChanged: {
        updateFavorites();
        gridView.forceActiveFocus();
        gridView.currentIndex = 0;
    }

    Component.onCompleted: {
        updateFavorites();
        gridView.forceActiveFocus();
    }

    function updateFavorites() {
        if (activeFace === 0) {
            // Show gallery favorites
            currentFavorites = favoritesController.getFavoriteGalleries();
        } else {
            // Show image favorites
            currentFavorites = favoritesController.getFavoriteImages();
        }
        gridView.model = currentFavorites.length;
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            root.back();
            event.accepted = true;
        } else if (event.key === Qt.Key_Tab) {
            activeFace = (activeFace + 1) % 2;
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (gridView.currentIndex >= 0 &&
                gridView.currentIndex < currentFavorites.length) {
                let item = currentFavorites[gridView.currentIndex];
                if (item.is_gallery) {
                    root.openGallery(item.path);
                } else {
                    // contract: WS3 openAlbum
                    root.openAlbum(
                        currentFavorites.map(it => it.nodeKey || 0),
                        gridView.currentIndex
                    );
                }
            }
            event.accepted = true;
        }
        // Arrow keys: let GridView handle them natively (currentIndex follows)
    }

    focus: true
}
