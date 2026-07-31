import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs

Dialog {
    id: dialog
    title: "Save Search"
    modal: true
    anchors.centerIn: parent

    // themePalette / advancedSearchController come from engine context properties
    // (shadowing property declarations break self-named bindings — T3.1 W5).
    // Public: set these before opening
    property var existingNames: []  // List of existing saved search names
    property string errorMessage: ""

    // Result signal (empty text = success, non-empty = error message)
    signal saved(string errorText)

    contentItem: Column {
        spacing: 12
        width: 300
        padding: 16

        Text {
            text: "Save this search as:"
            color: themePalette.text
            wrapMode: Text.Wrap
            width: parent.width
        }

        TextField {
            id: nameField
            width: parent.width
            text: ""
            selectByMouse: true
            color: themePalette.text
            background: Rectangle {
                color: themePalette.surface
                border.color: themePalette.border
                border.width: 1
                radius: 4
            }
            placeholderText: "Search name"
            placeholderTextColor: themePalette.textFaint

            Component.onCompleted: {
                forceActiveFocus()
            }

            Keys.onReturnPressed: {
                dialog.accept()
            }
            Keys.onEscapePressed: {
                dialog.reject()
            }

            // Clear error message on input change
            onTextChanged: {
                dialog.errorMessage = ""
            }
        }

        // Inline error display
        Text {
            width: parent.width
            text: dialog.errorMessage
            color: themePalette.danger
            wrapMode: Text.Wrap
            font.pixelSize: 11
            visible: text.length > 0
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
                text: "Save"
                onClicked: dialog.accept()
                background: Rectangle {
                    color: themePalette.accent
                    border.color: themePalette.accent
                    border.width: 1
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: themePalette.bg
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    onAccepted: {
        const newName = nameField.text.trim();
        if (newName.length === 0) {
            errorMessage = "Name cannot be empty";
            return;
        }

        // Check for duplicates
        if (existingNames.includes(newName)) {
            errorMessage = "A search with this name already exists";
            return;
        }

        // Success; close and signal
        saved("");
        close();
    }

    onRejected: {
        // cancelled, don't signal anything
    }

    onOpened: {
        // Clear error message when dialog is opened
        errorMessage = ""
        nameField.text = ""
        nameField.forceActiveFocus()
    }

    // Public function to get the entered name
    function getSearchName() {
        return nameField.text.trim();
    }
}
