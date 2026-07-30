import QtQuick
import QtQuick.Controls

Rectangle {
    id: helpPopup
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0)  // Transparent initially
    visible: false
    z: 1000  // Above other content

    // Public API
    property var helpModel: null

    // Internal state
    property bool isOpen: false

    // Computed properties
    property int columnCount: width > 1000 ? 2 : 1
    property real columnWidth: columnCount === 2 ? (contentPanel.width - 20) / 2 : contentPanel.width
    property real lineHeight: 24

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
        scrollView.focus = true;
        scrollView.forceActiveFocus();
    }

    function close() {
        if (!isOpen) return;
        isOpen = false;
        visible = false;
        veilRect.opacity = 0;
        contentPanel.opacity = 0;
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
            onClicked: helpPopup.close()
        }
    }

    // Content panel (centered)
    Rectangle {
        id: contentPanel
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.8, 1000)
        height: Math.min(parent.height * 0.8, 600)
        color: themePalette.surface
        border.color: themePalette.border
        border.width: 1
        radius: 8
        opacity: 0
        z: 1

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        // Header
        Rectangle {
            id: header
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 40
            color: themePalette.surfaceHi
            border.bottom.color: themePalette.border
            border.bottom.width: 1
            radius: 8

            Text {
                anchors {
                    left: parent.left
                    verticalCenter: parent.verticalCenter
                    leftMargin: 16
                }
                text: "F1 Help"
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
                text: "(Esc/Q to close)"
                color: themePalette.textDim
                font.pixelSize: 11
            }
        }

        // Scrollable content
        ScrollView {
            id: scrollView
            anchors {
                top: header.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            clip: true
            focus: true

            // Arrow keys scroll
            Keys.onUpPressed: {
                scrollView.ScrollBar.vertical.decrease();
                event.accepted = true;
            }
            Keys.onDownPressed: {
                scrollView.ScrollBar.vertical.increase();
                event.accepted = true;
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Escape || event.key === Qt.Key_Q) {
                    helpPopup.close();
                    event.accepted = true;
                }
            }

            Column {
                width: scrollView.width - scrollView.ScrollBar.vertical.width
                spacing: 0
                padding: 12

                Repeater {
                    model: helpModel ? helpModel.groups : []

                    Column {
                        width: parent.width - parent.padding * 2
                        spacing: 8

                        // Group title
                        Text {
                            width: parent.width
                            text: modelData.title
                            color: themePalette.accent
                            font.pixelSize: 13
                            font.bold: true
                            wrapMode: Text.Wrap
                        }

                        // Group entries
                        Column {
                            width: parent.width
                            spacing: 4

                            Repeater {
                                model: modelData.entries ? modelData.entries : []

                                Row {
                                    width: parent.width
                                    spacing: 12

                                    Text {
                                        width: 80
                                        text: modelData.keys
                                        color: themePalette.textDim
                                        font.pixelSize: 11
                                        font.family: "monospace"
                                        horizontalAlignment: Text.AlignLeft
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.description
                                        color: themePalette.text
                                        font.pixelSize: 11
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }

                        // Spacer between groups (not before first)
                        Rectangle {
                            width: parent.width
                            height: index < (helpModel ? helpModel.groups.length - 1 : 0) ? 8 : 0
                            color: "transparent"
                        }
                    }
                }
            }
        }
    }
}
