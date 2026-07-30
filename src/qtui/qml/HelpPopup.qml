import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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

    // Signal emitted when popup closes to allow focus restoration
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
        scrollView.focus = true;
        scrollView.forceActiveFocus();
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

        // Scrollable content with two-column reflow
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

            // Two-column layout: groups pack into columns, never split mid-group
            ColumnLayout {
                width: scrollView.width - scrollView.ScrollBar.vertical.width
                spacing: 0

                // Create columns based on columnCount
                Row {
                    Layout.fillWidth: true
                    spacing: 16

                    // Column 1 (always present)
                    Column {
                        width: helpPopup.columnCount === 2
                               ? (parent.width - 16) / 2
                               : parent.width
                        spacing: 8
                        padding: 12

                        // Groups that go in column 1
                        Repeater {
                            model: helpModel ? helpModel.groups : []

                            // Only show groups assigned to column 1
                            Item {
                                width: parent.width - parent.padding * 2
                                height: groupColumn.height
                                visible: helpPopup.columnCount === 1 ||
                                         index < Math.ceil((helpModel.groups.length) / 2)

                                Column {
                                    id: groupColumn
                                    width: parent.width
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
                                                    width: parent.width - 80 - parent.spacing
                                                    text: modelData.description
                                                    color: themePalette.text
                                                    font.pixelSize: 11
                                                    wrapMode: Text.Wrap
                                                }
                                            }
                                        }
                                    }

                                    // Spacer between groups
                                    Rectangle {
                                        width: parent.width
                                        height: (helpPopup.columnCount === 1 &&
                                                 index < (helpModel ? helpModel.groups.length - 1 : 0))
                                                ? 8 : 0
                                        color: "transparent"
                                    }
                                }
                            }
                        }
                    }

                    // Column 2 (only if two-column mode)
                    Column {
                        width: (parent.width - 16) / 2
                        spacing: 8
                        padding: 12
                        visible: helpPopup.columnCount === 2

                        // Groups that go in column 2
                        Repeater {
                            model: helpModel ? helpModel.groups : []

                            // Only show groups assigned to column 2
                            Item {
                                width: parent.width - parent.padding * 2
                                height: groupColumn2.height
                                visible: index >= Math.ceil((helpModel.groups.length) / 2)

                                Column {
                                    id: groupColumn2
                                    width: parent.width
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
                                                    width: parent.width - 80 - parent.spacing
                                                    text: modelData.description
                                                    color: themePalette.text
                                                    font.pixelSize: 11
                                                    wrapMode: Text.Wrap
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
