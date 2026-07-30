import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Osv 1.0

Rectangle {
    id: createRoot
    color: themePalette.bg
    anchors.fill: parent
    focus: true

    // Signal when vault creation is complete (regardless of success/failure)
    signal vaultCreated()

    property int currentFocus: 0  // 0=path, 1=password, 2=confirm, 3=keyfile, 4=create button

    // File dialog for choosing the save location
    FileDialog {
        id: saveFileDialog
        fileMode: FileDialog.SaveFile
        nameFilters: ["Vault files (*.osv)", "All files (*)"]
        defaultSuffix: "osv"
        onAccepted: {
            vaultPathField.text = saveFileDialog.selectedFile.toString();
            createRoot.currentFocus = 1;
            passwordField.forceActiveFocus();
        }
    }

    // File dialog for optional keyfile
    FileDialog {
        id: keyfileDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["All files (*)"]
        onAccepted: {
            keyfilePath = keyfileDialog.selectedFile;
            createRoot.currentFocus = 4;
            createButton.forceActiveFocus();
        }
    }

    property url keyfilePath: ""

    Keys.onEscapePressed: {
        passwordField.clearSecret();
        confirmField.clearSecret();
        createRoot.vaultCreated();
        if (parent && typeof parent.pop === 'function') parent.pop();
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Tab) {
            currentFocus = (currentFocus + 1) % 5;
            updateFocus();
            event.accepted = true;
        } else if (event.key === Qt.Key_Return) {
            if (currentFocus === 4) {
                attemptCreate();
                event.accepted = true;
            }
        }
    }

    function updateFocus() {
        if (currentFocus === 0) {
            vaultPathButton.forceActiveFocus();
        } else if (currentFocus === 1) {
            passwordField.forceActiveFocus();
        } else if (currentFocus === 2) {
            confirmField.forceActiveFocus();
        } else if (currentFocus === 3) {
            keyfileButton.forceActiveFocus();
        } else if (currentFocus === 4) {
            createButton.forceActiveFocus();
        }
    }

    function attemptCreate() {
        // Get the vault path
        const url = vaultPathField.text.length > 0
                    ? vaultPathField.text
                    : QUrl.fromLocalFile("/tmp/vault.osv");

        // Call the unlock controller to create the vault
        if (unlockController.createVault(url, passwordField, confirmField, keyfilePath)) {
            // Success: clear fields and go back
            vaultPathField.text = "";
            passwordField.clearSecret();
            confirmField.clearSecret();
            keyfilePath = "";
            createRoot.vaultCreated();
            if (parent && typeof parent.pop === 'function') parent.pop();
        } else {
            // Failure: show error in status bar
            statusController.set(2, "Create failed: " + unlockController.errorText);
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 16
        width: 420

        Text {
            width: parent.width
            text: "Create New Vault"
            color: themePalette.text
            font.pixelSize: 20
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Vault path
        Column {
            width: parent.width
            spacing: 4

            Text {
                text: "Save as:"
                color: themePalette.text
                font.pixelSize: 11
            }

            Row {
                width: parent.width
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    width: parent.width - 72
                    height: 36
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1
                    radius: 4

                    TextInput {
                        id: vaultPathField
                        anchors { fill: parent; margins: 8 }
                        color: themePalette.text
                        readOnly: true
                        text: "vault.osv"
                    }
                }

                Rectangle {
                    id: vaultPathButton
                    width: 64
                    height: 36
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1
                    radius: 4
                    focus: createRoot.currentFocus === 0

                    Text {
                        anchors.centerIn: parent
                        text: "Browse…"
                        color: themePalette.text
                        font.pixelSize: 11
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: saveFileDialog.open()
                    }

                    Keys.onReturnPressed: saveFileDialog.open()
                }
            }
        }

        // Password
        Column {
            width: parent.width
            spacing: 4

            Text {
                text: "Password:"
                color: themePalette.text
                font.pixelSize: 11
            }

            SecureTextField {
                id: passwordField
                width: parent.width
                height: 36
                focus: createRoot.currentFocus === 1

                Rectangle {
                    anchors.fill: parent
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1
                    radius: 4
                    z: -1
                }
            }

            // Strength indicator (live feedback)
            Rectangle {
                width: parent.width
                height: 8
                color: themePalette.surface
                border.color: themePalette.border
                border.width: 1
                radius: 4
                clip: true

                Rectangle {
                    height: parent.height - 2
                    y: 1
                    width: (strengthEstimate / 100) * (parent.width - 2)
                    x: 1
                    color: strengthEstimate < 33 ? themePalette.danger
                           : strengthEstimate < 66 ? themePalette.warn
                           : themePalette.ok
                    radius: 2
                }

                property real strengthEstimate: 0  // TODO: Wire to passphrase strength
            }

            Text {
                width: parent.width
                text: "Tip: Longer passwords are stronger"
                color: themePalette.textFaint
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }

        // Confirm password
        Column {
            width: parent.width
            spacing: 4

            Text {
                text: "Confirm Password:"
                color: themePalette.text
                font.pixelSize: 11
            }

            SecureTextField {
                id: confirmField
                width: parent.width
                height: 36
                focus: createRoot.currentFocus === 2

                Rectangle {
                    anchors.fill: parent
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1
                    radius: 4
                    z: -1
                }
            }
        }

        // Optional keyfile (Task 1.3 will complete this)
        Column {
            width: parent.width
            spacing: 4

            Text {
                text: "Optional: Keyfile"
                color: themePalette.text
                font.pixelSize: 11
            }

            Row {
                width: parent.width
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    width: parent.width - 72
                    height: 36
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1
                    radius: 4

                    Text {
                        anchors { fill: parent; margins: 8 }
                        text: keyfilePath.toString().length > 0
                              ? keyfilePath.toString().split('/').pop()
                              : "[none]"
                        color: themePalette.textDim
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }
                }

                Rectangle {
                    id: keyfileButton
                    width: 64
                    height: 36
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1
                    radius: 4
                    focus: createRoot.currentFocus === 3

                    Text {
                        anchors.centerIn: parent
                        text: "Browse…"
                        color: themePalette.text
                        font.pixelSize: 11
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: keyfileDialog.open()
                    }

                    Keys.onReturnPressed: keyfileDialog.open()
                }
            }
        }

        // Error message
        Text {
            width: parent.width
            text: unlockController.errorText
            color: themePalette.danger
            font.pixelSize: 12
            wrapMode: Text.Wrap
            visible: text.length > 0
        }

        // Create button
        Rectangle {
            id: createButton
            width: parent.width
            height: 44
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4
            focus: createRoot.currentFocus === 4

            MouseArea {
                anchors.fill: parent
                onClicked: attemptCreate()
            }

            Text {
                anchors.centerIn: parent
                text: "Create Vault"
                color: themePalette.text
                font.pixelSize: 14
            }

            Keys.onReturnPressed: attemptCreate()
        }

        // Cancel button
        Rectangle {
            width: parent.width
            height: 44
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    passwordField.clearSecret();
                    confirmField.clearSecret();
                    createRoot.vaultCreated();
                    if (parent && typeof parent.pop === 'function') parent.pop();
                }
            }

            Text {
                anchors.centerIn: parent
                text: "Cancel"
                color: themePalette.text
                font.pixelSize: 14
            }
        }
    }
}
