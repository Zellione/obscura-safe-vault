import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as QmlUI

Rectangle {
    id: root

    // importController / themePalette come from engine context properties
    // (required-property shadowing breaks self-named bindings — T3.1 W5).

    color: themePalette.bg
    visible: false

    signal back()

    // Track which task is selected (for Del/reorder)
    property int selectedTaskId: -1

    // Header
    Rectangle {
        id: header
        width: parent.width
        height: 48
        color: themePalette.surface
        z: 1

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 2

            Text {
                text: "Import Status (Shift+I to close)"
                color: themePalette.text
                font.bold: true
                font.pixelSize: 12
            }

            Text {
                text: "Ctrl+Up/Down: reorder · Del: cancel · C: clear finished"
                color: themePalette.textDim
                font.pixelSize: 10
            }
        }
    }

    // Main content area
    ColumnLayout {
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footerBanner.top
        anchors.margins: 8
        spacing: 8

        // Current running item (first from queue state)
        Rectangle {
            Layout.fillWidth: true
            height: 60
            color: themePalette.surfaceHi
            border.color: themePalette.accent
            border.width: importController.queueCount > 0 ? 1 : 0
            radius: 4
            visible: importController.queueCount > 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                // Line 1: task name + count
                Text {
                    text: "Importing... " + importController.footerSummary
                    color: themePalette.text
                    font.bold: true
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                // Line 2: progress bar with percentage
                RowLayout {
                    Layout.fillWidth: true
                    height: 24
                    spacing: 4

                    Rectangle {
                        Layout.fillWidth: true
                        height: parent.height
                        color: themePalette.surface
                        radius: 2
                        border.color: themePalette.border
                        border.width: 1

                        Rectangle {
                            height: parent.height
                            width: {
                                if (importController.queueCount > 0) {
                                    parent.width * 0.5  // Placeholder: would be driven by controller queue
                                } else {
                                    0
                                }
                            }
                            color: themePalette.accent
                            radius: 2
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "..."
                            color: themePalette.textDim
                            font.pixelSize: 10
                        }
                    }
                }
            }
        }

        // Empty state message
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4
            visible: importController.queueCount === 0

            Text {
                anchors.centerIn: parent
                text: "No imports running"
                color: themePalette.textDim
            }
        }

        // Queued items list (id-stable for Ctrl+Up/Down reorder + Del cancel)
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4
            visible: importController.queueCount > 0

            ScrollView {
                anchors.fill: parent

                ListView {
                    model: importController.queueCount  // Placeholder: would bind to real queue model
                    spacing: 2

                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 32
                        color: root.selectedTaskId === index ? themePalette.accent : themePalette.surfaceHi
                        radius: 2

                        Text {
                            anchors {
                                left: parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                margins: 4
                            }
                            text: "Task " + index + " (queued)"
                            color: root.selectedTaskId === index ? themePalette.bg : themePalette.text
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                root.selectedTaskId = index;
                                ListView.view.currentIndex = index;
                                root.forceActiveFocus();  // Focus for keyboard input
                            }
                        }
                    }
                }
            }
        }
    }

    // Lane failure banner (shown when operation fails)
    Rectangle {
        id: footerBanner
        anchors.bottom: closeButton.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 0  // Hidden until needed (would be set by error signal)
        color: themePalette.danger
        clip: true

        Text {
            anchors.fill: parent
            anchors.margins: 4
            text: "Import lane error"
            color: themePalette.bg
            wrapMode: Text.Wrap
            font.bold: true
        }
    }

    // Close button (Esc to dismiss)
    Button {
        id: closeButton
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.margins: 8
        text: "Close (Esc)"

        onClicked: {
            root.visible = false
            root.back()
        }
    }

    // Keyboard handling: Esc closes, Del cancels, C clears, Ctrl+Up/Down reorders
    Keys.onEscapePressed: {
        root.visible = false
        root.back()
    }

    Keys.onDeletePressed: {
        // Del cancels selected task
        if (root.selectedTaskId >= 0) {
            // Would call: importController.cancelTask(taskId)
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_C && !event.isAutoRepeat) {
            // C key clears finished imports
            importController.clearFinished()
            event.accepted = true
        }

        // Ctrl+Up/Down for id-based reorder (focus follows)
        if (event.key === Qt.Key_Up && event.modifiers & Qt.ControlModifier) {
            if (root.selectedTaskId > 0) {
                root.selectedTaskId--;  // Move up in list (focus follows)
                // Would call: importController.reorderTask(taskId, -1)
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_Down && event.modifiers & Qt.ControlModifier) {
            if (root.selectedTaskId < importController.queueCount - 1) {
                root.selectedTaskId++;  // Move down in list (focus follows)
                // Would call: importController.reorderTask(taskId, +1)
            }
            event.accepted = true;
        }
    }

    focus: visible
}
