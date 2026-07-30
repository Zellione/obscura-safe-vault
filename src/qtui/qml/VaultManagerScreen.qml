import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Osv 1.0

Rectangle {
    id: managerRoot
    color: themePalette.bg
    anchors.fill: parent

    // Help groups for F1 help popup
    property var helpGroups: [
        {
            title: "Vault Manager",
            entries: [
                { keys: "Enter", description: "Unlock vault" },
                { keys: "Arrow Up/Down", description: "Select vault" },
                { keys: "Delete", description: "Remove from list" },
                { keys: "O", description: "Open vault from disk" },
                { keys: "N", description: "Create new vault" },
                { keys: "C", description: "Settings" }
            ]
        }
    ]

    // Signals for navigation
    signal openVault(url vaultPath)
    signal createVault()

    // Current selection in the list (-1: none)
    property int currentSelection: 0

    // Re-read registry each time the screen appears
    Component.onCompleted: vaultManagerController.refreshVaultList()
    StackView.onActivated: vaultManagerController.refreshVaultList()

    // File dialog for opening from disk
    FileDialog {
        id: openFileDialog
        fileMode: FileDialog.OpenFile
        nameFilters: ["Vault files (*.osv)", "All files (*)"]
        onAccepted: {
            const url = openFileDialog.selectedFile;
            vaultManagerController.addVaultToRegistry(url);
            managerRoot.openVault(url);
        }
    }

    // Confirm dialog for deletion
    ConfirmDialog {
        id: deleteConfirmDialog
        title: "Remove Vault from List"
        body: "This removes the vault from your list, but does not delete the file."
        danger: true
        onAccepted: {
            if (currentSelection >= 0 && vaultManagerController.removeVaultFromRegistry(currentSelection)) {
                vaultManagerController.vaultListModel.refresh();
                // Move selection up if we deleted the last item
                const maxRow = vaultManagerController.vaultListModel.count - 1;
                if (currentSelection > maxRow && maxRow >= 0) {
                    currentSelection = maxRow;
                }
            }
        }
    }

    // Global key handlers
    Keys.onPressed: (event) => {
        if (event.text === "o" || event.text === "O") {
            openFileDialog.open();
            event.accepted = true;
        } else if (event.text === "n" || event.text === "N") {
            managerRoot.createVault();
            event.accepted = true;
        } else if (event.text === "c" || event.text === "C") {
            settingsOverlay.open();
            event.accepted = true;
        } else if (event.key === Qt.Key_Delete) {
            if (currentSelection >= 0 && vaultManagerController.vaultListModel.count > 0) {
                deleteConfirmDialog.open();
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Up) {
            currentSelection = Math.max(0, currentSelection - 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Down) {
            const maxRow = vaultManagerController.vaultListModel.count - 1;
            if (maxRow >= 0) {
                currentSelection = Math.min(currentSelection + 1, maxRow);
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Return) {
            if (currentSelection >= 0) {
                const url = vaultManagerController.vaultListModel.fileUrlAt(currentSelection);
                if (url.toString().length > 0) {
                    managerRoot.openVault(url);
                }
            }
            event.accepted = true;
        }
    }

    // Content
    Column {
        anchors.centerIn: parent
        spacing: 16
        width: 420

        // Title
        Text {
            width: parent.width
            text: "Obscura Safe Vault"
            color: themePalette.text
            font.pixelSize: 24
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
        }

        // Known vaults list
        Rectangle {
            width: parent.width
            height: Math.min(vaultList.contentHeight + 2, 264)
            visible: vaultManagerController.vaultListModel.count > 0
            color: "transparent"
            border.color: themePalette.border
            border.width: 1
            radius: 4
            clip: true

            ListView {
                id: vaultList
                anchors.fill: parent
                anchors.margins: 1
                model: vaultManagerController.vaultListModel
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: vaultList.width
                    height: 52
                    color: managerRoot.currentSelection === index
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
                            managerRoot.currentSelection = index;
                            vaultList.forceActiveFocus();
                        }
                        onDoubleClicked: {
                            managerRoot.currentSelection = index;
                            const url = vaultManagerController.vaultListModel.fileUrlAt(index);
                            if (url.toString().length > 0) {
                                managerRoot.openVault(url);
                            }
                        }
                    }
                }
            }
        }

        // Open button
        Rectangle {
            width: parent.width
            height: 44
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4

            MouseArea {
                anchors.fill: parent
                onClicked: openFileDialog.open()
            }

            Text {
                anchors.centerIn: parent
                text: vaultManagerController.vaultListModel.count > 0 ? "Open Other Vault…" : "Open Vault"
                color: themePalette.text
                font.pixelSize: 14
            }
        }

        // Create button
        Rectangle {
            width: parent.width
            height: 44
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4

            MouseArea {
                anchors.fill: parent
                onClicked: managerRoot.createVault()
            }

            Text {
                anchors.centerIn: parent
                text: "Create New Vault"
                color: themePalette.text
                font.pixelSize: 14
            }
        }

        // Hints
        Column {
            width: parent.width
            spacing: 4

            Text {
                width: parent.width
                text: "Keyboard shortcuts:"
                color: themePalette.textDim
                font.pixelSize: 11
            }

            Text {
                width: parent.width
                text: "Enter: unlock  •  O: open  •  N: create  •  Del: remove  •  C: settings"
                color: themePalette.textFaint
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
    }
}
