import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Dialog {
    id: dialog
    title: "Confirm"
    modal: true
    anchors.centerIn: parent

    // Public: set these before opening
    property string titleText: "Confirm"
    property string bodyText: ""
    property bool danger: false  // true switches border to danger color

    // Result signals
    signal confirmed()
    signal cancelled()

    contentItem: Column {
        spacing: 12
        width: 300
        padding: 16

        Text {
            text: dialog.bodyText
            color: themePalette.text
            wrapMode: Text.Wrap
            width: parent.width
        }

        Row {
            spacing: 8
            anchors.right: parent.right

            Button {
                id: cancelBtn
                text: "Cancel"
                onClicked: dialog.reject()

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

                Component.onCompleted: {
                    // Default focus on cancel button
                    forceActiveFocus();
                }

                Keys.onEscapePressed: {
                    dialog.reject()
                }
            }

            Button {
                id: confirmBtn
                text: "OK"
                onClicked: dialog.accept()

                background: Rectangle {
                    color: dialog.danger ? themePalette.danger : themePalette.accent
                    border.color: color
                    border.width: 1
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: themePalette.bg
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                Keys.onReturnPressed: {
                    dialog.accept()
                }
                Keys.onEnterPressed: {
                    dialog.accept()
                }
            }
        }
    }

    // Keyboard shortcuts: N for no, Y for yes
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_N || event.text === "n" || event.text === "N") {
            dialog.reject()
            event.accepted = true
        } else if (event.key === Qt.Key_Y || event.text === "y" || event.text === "Y") {
            dialog.accept()
            event.accepted = true
        }
    }

    onAccepted: {
        dialog.confirmed()
    }

    onRejected: {
        dialog.cancelled()
    }

    onOpened: {
        // Set proper title
        title = dialog.titleText
    }
}
