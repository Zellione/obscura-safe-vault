import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root
    color: themePalette.bg

    required property var themePalette
    required property var favoritesController
    required property var galleryModel
    required property var viewerController
    required property var selectionController

    signal back()
    signal openGallery(nodePath: string)
    signal openViewer(nodeKey: int)

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
                width: gridView.cellWidth - 4
                height: gridView.cellHeight - 4
                color: themePalette.surface
                border.color: (selectionController.isSelected(modelData.path))
                    ? themePalette.accent
                    : themePalette.border
                border.width: 2

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: themePalette.imgBg

                        // Thumbnail image for media
                        SecureImageItem {
                            visible: !modelData.is_gallery
                            anchors.fill: parent
                            nodeKey: modelData.nodeKey
                        }

                        // Folder glyph for galleries
                        Text {
                            visible: modelData.is_gallery
                            anchors.centerIn: parent
                            text: "📁"
                            font.pixelSize: 36
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: modelData.name
                        color: themePalette.text
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        selectionController.setSelected(modelData.path, true);
                    }
                    onDoubleClicked: {
                        if (modelData.is_gallery) {
                            root.openGallery(modelData.path);
                        } else {
                            // Open in collection mode (WS3 contract)
                            // Find the index in the currentFavorites array
                            let index = -1;
                            for (let i = 0; i < currentFavorites.length; i++) {
                                if (currentFavorites[i].path === modelData.path) {
                                    index = i;
                                    break;
                                }
                            }
                            if (index >= 0) {
                                // contract: WS3 openAlbum
                                viewerController.openAlbum(
                                    currentFavorites.map(item => item.nodeKey || 0),
                                    index
                                );
                            }
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
    }

    Component.onCompleted: {
        updateFavorites();
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
            if (selectionController.currentRow >= 0 &&
                selectionController.currentRow < currentFavorites.length) {
                let item = currentFavorites[selectionController.currentRow];
                if (item.is_gallery) {
                    root.openGallery(item.path);
                } else {
                    // contract: WS3 openAlbum
                    viewerController.openAlbum(
                        currentFavorites.map(it => it.nodeKey || 0),
                        selectionController.currentRow
                    );
                }
            }
            event.accepted = true;
        }
    }

    focus: true
}
