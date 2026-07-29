import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Osv 1.0

Window {
    id: root
    width: 1280
    height: 800
    visible: true
    color: "#14161a"
    title: "osv-qt (experiment)"

    StackView {
        id: stack
        anchors.fill: parent

        initialItem: unlockScreenComponent

        Connections {
            target: unlockController
            function onUnlockedChanged() {
                if (unlockController.unlocked) {
                    stack.replace(unlockedPageComponent)
                } else {
                    stack.replace(unlockScreenComponent)
                }
            }
        }

        // Inline unlock screen (was UnlockScreen.qml)
        Component {
            id: unlockScreenComponent
            Rectangle {
                id: unlockRoot
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
        }

        // Unlocked page with image viewer
        Component {
            id: unlockedPageComponent
            Rectangle {
                color: "#14161a"
                SecureImageItem {
                    anchors.fill: parent
                    Component.onCompleted: unlockController.loadFirstImage(this)
                }
            }
        }
    }
}
