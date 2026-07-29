import QtQuick
import QtQuick.Dialogs

Rectangle {
    id: root
    color: "#14161a"

    FileDialog {
        id: fileDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Vault files (*.osv)", "All files (*)"]
        onAccepted: {
            unlockController.openVault(fileDialog.selectedFile)
            passwordField.focus = true
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 16
        width: 300

        // Vault file picker
        Rectangle {
            width: parent.width
            height: 44
            color: "#2a2d33"
            border.color: "#3f4349"
            border.width: 1
            radius: 4

            MouseArea {
                anchors.fill: parent
                onClicked: fileDialog.open()
            }

            Text {
                anchors.centerIn: parent
                text: "Open Vault"
                color: "#c8ccd4"
                font.pixelSize: 14
            }
        }

        // Password field
        SecureTextField {
            id: passwordField
            width: parent.width
            height: 44
            focus: false

            Rectangle {
                anchors.fill: parent
                color: "#2a2d33"
                border.color: "#3f4349"
                border.width: 1
                radius: 4
                z: -1
            }

            onAccepted: {
                unlockController.unlock(passwordField)
            }
        }

        // Unlock button
        Rectangle {
            width: parent.width
            height: 44
            color: "#2a2d33"
            border.color: "#3f4349"
            border.width: 1
            radius: 4

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    unlockController.unlock(passwordField)
                }
            }

            Text {
                anchors.centerIn: parent
                text: "Unlock"
                color: "#c8ccd4"
                font.pixelSize: 14
            }
        }

        // Error message
        Text {
            width: parent.width
            text: unlockController.errorText
            color: "#ff6b6b"
            font.pixelSize: 12
            wrapMode: Text.Wrap
            visible: text.length > 0
        }
    }
}
