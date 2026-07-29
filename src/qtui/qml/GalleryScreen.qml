import QtQuick
import QtQuick.Controls
import Osv 1.0

// Gallery grid view: displays galleries and media from galleryModel.
// Galleries shown as folder glyphs, media as thumbnails.
// Arrow keys navigate, Enter/Escape open/go back.
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

    // Arrow keys for grid navigation (built-in to GridView)
}
