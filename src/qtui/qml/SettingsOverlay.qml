import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: settingsOverlay
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0)  // Transparent initially
    visible: false
    z: 1000  // Above other content

    // Public API
    property var settingsController: null

    // Internal state
    property bool isOpen: false

    // Sections: 0=Appearance, 1=Browsing, 2=TagColours
    property int currentSection: 0

    // Signal emitted when overlay closes to allow focus restoration
    signal closed()

    // Called from Main.qml
    function toggle() {
        if (isOpen) {
            close();
        } else {
            open();
        }
    }

    function open() {
        if (isOpen) return;
        isOpen = true;
        visible = true;
        veilRect.opacity = 0.5;
        contentPanel.opacity = 1.0;
        contentPanel.forceActiveFocus();
    }

    function close() {
        if (!isOpen) return;
        isOpen = false;
        visible = false;
        veilRect.opacity = 0;
        contentPanel.opacity = 0;
        closed();  // Emit signal to restore focus
    }

    // Veil (semi-transparent overlay)
    Rectangle {
        id: veilRect
        anchors.fill: parent
        color: themePalette.veil
        opacity: 0
        z: -1

        MouseArea {
            anchors.fill: parent
            onClicked: settingsOverlay.close()
        }
    }

    // Content panel (veil + rail + pane)
    Rectangle {
        id: contentPanel
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, 1000)
        height: Math.min(parent.height * 0.85, 700)
        color: themePalette.surface
        border.color: themePalette.border
        border.width: 1
        radius: 8
        opacity: 0
        z: 1
        focus: true

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        // Header
        Rectangle {
            id: header
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 40
            color: themePalette.surfaceHi
            radius: 8

            // Bottom border line
            Rectangle {
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                height: 1
                color: themePalette.border
            }

            Text {
                anchors {
                    left: parent.left
                    verticalCenter: parent.verticalCenter
                    leftMargin: 16
                }
                text: "F2 Settings"
                color: themePalette.text
                font.pixelSize: 14
                font.bold: true
            }

            Text {
                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    rightMargin: 16
                }
                text: "(Esc to close)"
                color: themePalette.textDim
                font.pixelSize: 11
            }
        }

        // Body: horizontal layout (rail + pane)
        RowLayout {
            anchors {
                top: header.bottom
                left: parent.left
                right: parent.right
                bottom: footerLine.top
            }
            spacing: 0

            // Left rail: section selector
            Rectangle {
                Layout.preferredWidth: 160
                Layout.fillHeight: true
                color: themePalette.bg

                // Right border
                Rectangle {
                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                    width: 1
                    color: themePalette.border
                }

                Column {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                        topMargin: 8
                    }
                    spacing: 4

                    Repeater {
                        model: ["Appearance", "Browsing", "Tag Colours"]

                        Rectangle {
                            width: parent.width - 8
                            height: 36
                            anchors.horizontalCenter: parent.horizontalCenter
                            color: index === settingsOverlay.currentSection ? themePalette.surfaceHi : "transparent"
                            radius: 4

                            Text {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 12
                                }
                                text: modelData
                                color: themePalette.text
                                font.pixelSize: 12
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: settingsOverlay.currentSection = index
                            }
                        }
                    }
                }
            }

            // Right pane: content
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: themePalette.surface

                // Appearance section
                Column {
                    anchors {
                        fill: parent
                        margins: 16
                    }
                    spacing: 12
                    visible: settingsOverlay.currentSection === 0

                    Text {
                        text: "Theme"
                        color: themePalette.text
                        font.pixelSize: 12
                        font.bold: true
                    }

                    Row {
                        spacing: 12
                        anchors.left: parent.left

                        ComboBox {
                            width: 200
                            model: settingsController ? settingsController.themeList : []
                            currentIndex: settingsController ? settingsController.currentThemeIndex : 0
                            onCurrentIndexChanged: {
                                if (settingsController) {
                                    settingsController.setCurrentThemeIndex(currentIndex);
                                }
                            }
                        }
                    }
                }

                // Browsing section
                Column {
                    anchors {
                        fill: parent
                        margins: 16
                    }
                    spacing: 12
                    visible: settingsOverlay.currentSection === 1

                    Text {
                        text: settingsController && !settingsController.vaultUnlocked
                              ? "Unlock a vault to configure"
                              : "Browsing Defaults"
                        color: themePalette.text
                        font.pixelSize: 12
                        font.bold: true
                    }
                }

                // Tag Colours section
                Column {
                    anchors {
                        fill: parent
                        margins: 16
                    }
                    spacing: 12
                    visible: settingsOverlay.currentSection === 2

                    Text {
                        text: settingsController && !settingsController.vaultUnlocked
                              ? "Unlock a vault to configure"
                              : "Tag Categories"
                        color: themePalette.text
                        font.pixelSize: 12
                        font.bold: true
                    }

                    // Category list (placeholder)
                    Column {
                        width: parent.width
                        spacing: 4
                        visible: settingsController && settingsController.vaultUnlocked

                        Repeater {
                            model: settingsController ? settingsController.categories : []

                            Row {
                                width: parent.width
                                spacing: 8

                                Rectangle {
                                    width: 24
                                    height: 24
                                    radius: 4
                                    color: themePalette.text
                                    opacity: 0.3
                                }

                                Text {
                                    text: modelData.name
                                    color: themePalette.text
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
        }

        // Error line in footer
        Rectangle {
            id: footerLine
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 30
            color: themePalette.bg
            radius: 8

            Text {
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: 12
                    rightMargin: 12
                }
                text: settingsController ? settingsController.errorLine : ""
                color: settingsController && settingsController.errorLine.length > 0
                       ? themePalette.danger : "transparent"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        // Key handlers
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                settingsOverlay.close();
                event.accepted = true;
            }
        }
    }
}
