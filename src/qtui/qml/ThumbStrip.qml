import QtQuick

Rectangle {
    id: root

    // Properties
    property int stripSide: 0  // 0 = Bottom, 1 = Left (persisted in SessionState)
    property int currentIndex: -1  // Currently viewed item index in the strip
    // visible property is inherited from Rectangle (used by T key to toggle)

    // Colors from theme palette
    color: themePalette.stripBg

    // Anchoring and sizing based on strip position
    anchors {
        left: parent.left
        top: stripSide === 1 ? parent.top : undefined
        bottom: stripSide === 0 ? parent.bottom : undefined
        right: stripSide === 0 ? parent.right : undefined
    }

    width: stripSide === 0 ? parent.width : 120  // Left strip is narrow
    height: stripSide === 0 ? 120 : parent.height  // Bottom strip is short

    // Signal emitted when user clicks on a thumbnail to jump to it
    signal jumpToIndex(int index)

    // Internal: listview for displaying thumbnails
    ListView {
        id: thumbListView
        anchors.fill: parent
        anchors.margins: 10

        // Horizontal layout when at Bottom, vertical when at Left
        orientation: root.stripSide === 0 ? ListView.Horizontal : ListView.Vertical
        spacing: 10

        // Model will be set by ViewerScreen from galleryModel
        model: undefined

        // Thumbnail delegate: SecureImageItem in a clickable container
        delegate: Rectangle {
            id: thumbDelegate

            // Size depends on strip orientation
            width: root.stripSide === 0 ? 80 : thumbListView.width - 20
            height: root.stripSide === 0 ? thumbListView.height - 20 : 80

            color: root.currentIndex === index ? themePalette.accentDim : "transparent"
            radius: 4
            border.color: root.currentIndex === index ? themePalette.accent : "transparent"
            border.width: root.currentIndex === index ? 2 : 0

            // Clamp spacing when at boundaries
            ListView.onAdd: {
                if (root.currentIndex === index) {
                    thumbListView.positionViewAtIndex(index, ListView.Center);
                }
            }

            // Thumbnail image (placeholder for test; will be SecureImageItem in ViewerScreen)
            Rectangle {
                anchors {
                    fill: parent
                    margins: 2
                }
                color: themePalette.imgBg
            }

            // Click handler: jump to this item
            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.jumpToIndex(index);
                }
            }
        }

        // Mouse wheel scrolling support
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse
            onWheel: {
                // Scroll the list view
                const step = 3;  // scroll by 3 items at a time
                if (event.angleDelta.y > 0) {
                    // Scroll up/left
                    thumbListView.decrementCurrentIndex();
                    for (let i = 0; i < step - 1; i++) {
                        thumbListView.decrementCurrentIndex();
                    }
                } else {
                    // Scroll down/right
                    thumbListView.incrementCurrentIndex();
                    for (let i = 0; i < step - 1; i++) {
                        thumbListView.incrementCurrentIndex();
                    }
                }
            }
        }
    }
}
