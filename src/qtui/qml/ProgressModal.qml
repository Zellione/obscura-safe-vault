import QtQuick
import QtQuick.Controls

Rectangle {
    id: modal
    color: themePalette.veil
    anchors.fill: parent
    visible: false

    // Public properties
    property int done: 0
    property int total: 100
    property string title: "Operation in progress"
    property string cancelHint: "Press Esc or click Cancel to stop"

    // Signals
    signal cancelRequested()

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        // Prevent clicks from going through the veil
    }

    Column {
        anchors.centerIn: parent
        spacing: 16
        width: 300

        // Title
        Text {
            text: modal.title
            color: themePalette.text
            font.pixelSize: 16
            font.bold: true
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Progress counter
        Text {
            text: modal.total > 0
                ? (modal.done + " / " + modal.total + " items")
                : "Working..."
            color: themePalette.text
            font.pixelSize: 14
            anchors.horizontalCenter: parent.horizontalCenter
        }

        // Progress bar
        Rectangle {
            width: parent.width
            height: 24
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle {
                height: parent.height
                color: themePalette.accent
                radius: 4
                width: modal.total > 0
                    ? parent.width * (modal.done / modal.total)
                    : 0

                Behavior on width {
                    NumberAnimation { duration: 200 }
                }
            }

            Text {
                anchors.centerIn: parent
                text: modal.total > 0
                    ? Math.round(modal.done * 100 / modal.total) + "%"
                    : "0%"
                color: themePalette.text
                font.pixelSize: 12
            }
        }

        // Cancel hint
        Text {
            text: modal.cancelHint
            color: themePalette.textFaint
            font.pixelSize: 11
            wrapMode: Text.Wrap
            width: parent.width
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
        }

        // Cancel button
        Button {
            text: "Cancel"
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: modal.cancelRequested()

            background: Rectangle {
                color: themePalette.surfaceHi
                border.color: themePalette.border
                border.width: 1
                radius: 4
            }
            contentItem: Text {
                text: parent.text
                color: themePalette.text
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // Keyboard shortcut: Esc to cancel
    Keys.onEscapePressed: {
        modal.cancelRequested()
    }

    // Auto-hide when operation completes (when done == total or total == 0)
    onDoneChanged: {
        if (modal.total > 0 && modal.done >= modal.total) {
            Qt.callLater(function() { modal.visible = false })
        }
    }
}
