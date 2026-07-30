import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Osv 1.0

Rectangle {
    id: unlockRoot
    color: themePalette.bg

    // Row of the known-vault list currently opened (-1: none / file dialog)
    property int selectedVaultRow: -1
    property url selectedKeyfileUrl: ""

    // Help groups for F1 help popup
    property var helpGroups: [
        {
            title: "Unlock",
            entries: [
                { keys: "Enter", description: "Unlock vault" },
                { keys: "Arrow Up/Down", description: "Select vault" }
            ]
        }
    ]

    // Pick up registry/folder changes each time the screen appears
    Component.onCompleted: vaultListModel.refresh()

    FileDialog {
        id: vaultFileDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Vault files (*.osv)", "All files (*)"]
        onAccepted: {
            if (unlockController.openVault(vaultFileDialog.selectedFile)) {
                unlockRoot.selectedVaultRow = -1
                selectedKeyfileUrl = "";
            }
            passwordField.forceActiveFocus()
        }
    }

    FileDialog {
        id: keyfilePickerDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["All files (*)"]
        onAccepted: {
            selectedKeyfileUrl = keyfilePickerDialog.selectedFile;
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: 16
        width: 420

        // Known vaults: registry entries + *.osv in the per-user data
        // folder. That folder hides under ~/.local/share (hidden dir),
        // which the native file dialog won't show — this list makes
        // those vaults directly clickable.
        Rectangle {
            width: parent.width
            height: Math.min(vaultList.contentHeight + 2, 264)
            visible: vaultListModel.count > 0
            color: "transparent"
            border.color: themePalette.border
            border.width: 1
            radius: 4
            clip: true

            ListView {
                id: vaultList
                anchors.fill: parent
                anchors.margins: 1
                model: vaultListModel
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: vaultList.width
                    height: 52
                    color: unlockRoot.selectedVaultRow === index
                           ? themePalette.surfaceHi : themePalette.surface

                    Column {
                        anchors {
                            verticalCenter: parent.verticalCenter
                            left: parent.left
                            right: parent.right
                            leftMargin: 12
                            rightMargin: 12
                        }
                        spacing: 2

                        Text {
                            width: parent.width
                            text: model.name
                            color: themePalette.text
                            font.pixelSize: 14
                            elide: Text.ElideMiddle
                        }
                        Text {
                            width: parent.width
                            text: model.dir
                            color: themePalette.textDim
                            font.pixelSize: 11
                            elide: Text.ElideMiddle
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (unlockController.openVault(vaultListModel.fileUrlAt(index))) {
                                unlockRoot.selectedVaultRow = index
                                passwordField.forceActiveFocus()
                            }
                        }
                    }
                }
            }
        }

        // Vault file picker (for vaults not in the list)
        Rectangle {
            width: parent.width
            height: 44
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4

            MouseArea {
                anchors.fill: parent
                onClicked: vaultFileDialog.open()
            }

            Text {
                anchors.centerIn: parent
                text: vaultListModel.count > 0 ? "Open Other Vault…" : "Open Vault"
                color: themePalette.text
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
                color: themePalette.surface
                border.color: themePalette.border
                border.width: 1
                radius: 4
                z: -1
            }

            onAccepted: {
                if (unlockRoot.selectedKeyfileUrl.toString().length > 0) {
                    unlockController.unlockWithKeyfile(passwordField, selectedKeyfileUrl)
                } else {
                    unlockController.unlock(passwordField)
                }
            }
        }

        // Optional keyfile
        Column {
            width: parent.width
            spacing: 4

            Text {
                text: "Keyfile (optional):"
                color: themePalette.textDim
                font.pixelSize: 11
            }

            Row {
                width: parent.width
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    width: parent.width - 72
                    height: 40
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1
                    radius: 4

                    Text {
                        anchors { fill: parent; margins: 8 }
                        text: selectedKeyfileUrl.toString().length > 0
                              ? selectedKeyfileUrl.toString().split('/').pop()
                              : "[none]"
                        color: themePalette.textDim
                        font.pixelSize: 11
                        elide: Text.ElideMiddle
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: keyfilePickerDialog.open()
                    }
                }

                Rectangle {
                    width: 64
                    height: 40
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1
                    radius: 4

                    MouseArea {
                        anchors.fill: parent
                        onClicked: keyfilePickerDialog.open()
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "Browse"
                        color: themePalette.text
                        font.pixelSize: 11
                    }
                }
            }
        }

        // Unlock button
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
                    if (unlockRoot.selectedKeyfileUrl.toString().length > 0) {
                        unlockController.unlockWithKeyfile(passwordField, selectedKeyfileUrl)
                    } else {
                        unlockController.unlock(passwordField)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                text: "Unlock"
                color: themePalette.text
                font.pixelSize: 14
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
    }
}
