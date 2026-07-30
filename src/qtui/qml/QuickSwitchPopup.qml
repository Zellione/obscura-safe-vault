import QtQuick
import QtQuick.Controls
import Osv 1.0

// Quick-switch overlay: backtick (`) lists registry vaults, Enter locks and routes
Rectangle {
    id: quickSwitchRoot
    anchors.fill: parent
    color: themePalette.veil
    visible: false
    z: 2000

    property int currentIndex: 0
    property bool isOpen: false
    property url activeVaultPath: ""  // Path to the currently-unlocked vault

    signal switchToVault(url vaultPath)

    // Pick up registry changes when screen appears
    Component.onCompleted: vaultManagerController.refreshVaultList()
    onVisibleChanged: if (visible) vaultManagerController.refreshVaultList()

    // Keyboard handlers
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            quickSwitchRoot.close();
            event.accepted = true;
        } else if (event.key === Qt.Key_Up) {
            currentIndex = Math.max(0, currentIndex - 1);
            vaultList.positionViewAtIndex(currentIndex, ListView.Contain);
            event.accepted = true;
        } else if (event.key === Qt.Key_Down) {
            const maxIdx = vaultManagerController.vaultListModel.count - 1;
            if (maxIdx >= 0) {
                currentIndex = Math.min(currentIndex + 1, maxIdx);
                vaultList.positionViewAtIndex(currentIndex, ListView.Contain);
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Return) {
            selectCurrentVault();
            event.accepted = true;
        }
    }

    function open() {
        if (isOpen) return;
        isOpen = true;
        visible = true;
        currentIndex = 0;
        vaultList.forceActiveFocus();
    }

    function close() {
        if (!isOpen) return;
        isOpen = false;
        visible = false;
    }

    function selectCurrentVault() {
        if (currentIndex < 0 || currentIndex >= vaultManagerController.vaultListModel.count) {
            return;
        }

        const url = vaultManagerController.vaultListModel.fileUrlAt(currentIndex);
        if (url.toString().length === 0) {
            return;
        }

        // Check if this is the currently unlocked vault (no-op if selecting the active vault)
        if (url.toString() === activeVaultPath.toString()) {
            close();
            return;
        }

        // Lock current vault
        if (unlockController.unlocked) {
            unlockController.lock();
        }

        close();
        switchToVault(url);
    }

    // Veil (semi-transparent overlay)
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.7
        z: -1

        MouseArea {
            anchors.fill: parent
            onClicked: quickSwitchRoot.close()
        }
    }

    // Content panel
    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.6, 500)
        height: Math.min(parent.height * 0.7, 400)
        color: themePalette.surface
        border.color: themePalette.border
        border.width: 1
        radius: 8

        Column {
            anchors { fill: parent; margins: 16 }
            spacing: 12

            Text {
                text: "Switch Vault"
                color: themePalette.text
                font.pixelSize: 18
                font.bold: true
            }

            Rectangle {
                width: parent.width
                height: Math.min(vaultList.contentHeight, parent.height - 120)
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
                    focus: true

                    delegate: Rectangle {
                        width: vaultList.width
                        height: 52
                        color: quickSwitchRoot.currentIndex === index
                               ? themePalette.surfaceHi : themePalette.surface
                        border.color: model.fileUrl.toString() === quickSwitchRoot.activeVaultPath.toString()
                                      ? themePalette.accent : "transparent"
                        border.width: model.fileUrl.toString() === quickSwitchRoot.activeVaultPath.toString() ? 2 : 0

                        Column {
                            anchors {
                                verticalCenter: parent.verticalCenter
                                left: parent.left
                                right: parent.right
                                leftMargin: 12
                                rightMargin: 12
                            }
                            spacing: 2

                            Row {
                                width: parent.width
                                spacing: 8

                                Text {
                                    width: parent.width - currentSuffix.width
                                    text: model.name
                                    color: themePalette.text
                                    font.pixelSize: 14
                                    elide: Text.ElideMiddle
                                }

                                Text {
                                    id: currentSuffix
                                    text: model.fileUrl.toString() === quickSwitchRoot.activeVaultPath.toString()
                                          ? "(current)" : ""
                                    color: themePalette.textDim
                                    font.pixelSize: 12
                                }
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
                                quickSwitchRoot.currentIndex = index;
                            }
                            onDoubleClicked: {
                                quickSwitchRoot.currentIndex = index;
                                quickSwitchRoot.selectCurrentVault();
                            }
                        }
                    }
                }
            }

            Text {
                width: parent.width
                text: "↑/↓: navigate  •  Enter: switch  •  Esc: cancel"
                color: themePalette.textFaint
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
    }
}
