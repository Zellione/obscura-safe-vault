import QtQuick
import QtQuick.Controls
import Osv 1.0

// Detail panel for showing metadata about selected gallery items (WS2 Task 2.3)
// Visible when sessionState.detailOpen && parent.width >= 640
// Supports Ctrl+Up/Down + mouse wheel scroll
Rectangle {
    id: detailPanel
    color: themePalette.surface
    border.color: themePalette.border
    border.width: 1
    width: 280
    visible: sessionState.detailOpen && parent.width >= 640
    anchors {
        top: parent.top
        right: parent.right
        bottom: parent.bottom
    }

    // Flickable content area with scroll support
    Flickable {
        id: flickable
        anchors {
            fill: parent
            margins: 8
        }
        contentHeight: column.height
        clip: true

        Column {
            id: column
            width: flickable.width
            spacing: 12

            // Heading
            Text {
                width: parent.width
                text: detailController.heading
                color: themePalette.text
                font.pixelSize: 16
                font.bold: true
                elide: Text.ElideRight
                wrapMode: Text.Wrap
            }

            // Subheading (markers like favorite, animated)
            Text {
                visible: detailController.subheading.length > 0
                width: parent.width
                text: detailController.subheading
                color: themePalette.textDim
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            // Sections with rows and bullets
            Repeater {
                model: detailController.sectionCount()

                Column {
                    id: sectionColumn
                    width: column.width
                    spacing: 4

                    // Section title (if not empty)
                    Text {
                        visible: detailController.sectionTitle(index).length > 0
                        width: parent.width
                        text: detailController.sectionTitle(index)
                        color: themePalette.textDim
                        font.pixelSize: 11
                        font.bold: true
                    }

                    // Rows (label: value pairs)
                    Repeater {
                        model: detailController.rowCount(index)

                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                width: 80
                                text: detailController.rowLabel(index, modelData)
                                color: themePalette.textDim
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }

                            Text {
                                width: parent.width - 80 - 8  // parent width - label width - spacing
                                text: detailController.rowValue(index, modelData)
                                color: themePalette.text
                                font.pixelSize: 11
                                elide: Text.ElideRight
                                wrapMode: Text.Wrap
                            }
                        }
                    }

                    // Bullets (tags, etc.)
                    Repeater {
                        model: detailController.bulletCount(index)

                        Text {
                            width: parent.width
                            text: "• " + detailController.bullet(index, modelData)
                            color: detailController.isBulletTag(index) ? themePalette.accent : themePalette.text
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            // Spacer at end
            Rectangle {
                width: 1
                height: 8
                color: "transparent"
            }
        }
    }

    // Scroll bar on the right — TODO: wire to Flickable once Qt.labs.controls or proper attached object is available
    // For now, Flickable is directly scrollable via mouse wheel and keyboard (Ctrl+Up/Down)

    // Keyboard support: Ctrl+Up/Down to scroll
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Up && event.modifiers & Qt.ControlModifier) {
            flickable.contentY = Math.max(0, flickable.contentY - 48)
            event.accepted = true
        } else if (event.key === Qt.Key_Down && event.modifiers & Qt.ControlModifier) {
            flickable.contentY = Math.min(flickable.contentHeight - flickable.height,
                                         flickable.contentY + 48)
            event.accepted = true
        }
    }

    // Mouse wheel scroll support
    WheelHandler {
        target: flickable
        onWheel: (event) => {
            // Negative wheel.angleDelta.y means scrolling down (content moves up)
            var delta = -event.angleDelta.y * 0.05
            flickable.contentY = Math.max(0, Math.min(
                flickable.contentHeight - flickable.height,
                flickable.contentY + delta
            ))
        }
    }
}
