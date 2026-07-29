import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Dialog {
    id: dialog
    title: "Rename"
    modal: true
    anchors.centerIn: parent

    // Public: set these before opening
    property string originalName: ""
    property int targetRow: -1

    // Result signal (empty text = success, non-empty = error message)
    signal renamed(string errorText)

    contentItem: Column {
        spacing: 12
        width: 300
        padding: 16

        Text {
            text: "Enter new name for: " + dialog.originalName
            color: themePalette.text
            wrapMode: Text.Wrap
            width: parent.width
        }

        TextField {
            id: nameField
            width: parent.width
            text: dialog.originalName
            selectByMouse: true
            color: themePalette.text
            background: Rectangle {
                color: themePalette.surface
                border.color: themePalette.border
                border.width: 1
                radius: 4
            }
            placeholderText: "Name"
            placeholderTextColor: themePalette.textFaint

            Component.onCompleted: {
                selectAll()
                forceActiveFocus()
            }

            Keys.onReturnPressed: {
                dialog.accept()
            }
            Keys.onEscapePressed: {
                dialog.reject()
            }
        }

        Row {
            spacing: 8
            anchors.right: parent.right

            Button {
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
            }

            Button {
                text: "OK"
                onClicked: dialog.accept()
                background: Rectangle {
                    color: themePalette.accent
                    border.color: themePalette.accent
                    border.width: 1
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: "#000000"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    onAccepted: {
        const newName = nameField.text.trim();
        if (newName.length === 0) {
            renamed("Name cannot be empty");
            return;
        }

        if (newName === dialog.originalName) {
            renamed("");  // no-op, but success
            return;
        }

        const errorText = galleryModel.rename(dialog.targetRow, newName);
        renamed(errorText);
    }

    onRejected: {
        // cancelled, don't signal anything
    }
}
