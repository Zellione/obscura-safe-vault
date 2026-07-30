import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as QmlUI

Rectangle {
    id: root

    // Required properties (bound by shell)
    required property var importController
    required property var themePalette

    color: themePalette.bg
    visible: false

    signal back()

    // Header
    Rectangle {
        id: header
        width: parent.width
        height: 48
        color: themePalette.surface
        z: 1

        Text {
            text: "Import Status (Shift+I to close)"
            color: themePalette.text
            font.bold: true
            anchors.centerIn: parent
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

        // Current import (running) — two-line display
        Rectangle {
            Layout.fillWidth: true
            height: Math.max(50, contentHeight)
            color: themePalette.surfaceHi
            border.color: themePalette.accent
            border.width: importController.queueCount > 0 ? 1 : 0
            radius: 4

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                // Line 1: name + count
                Text {
                    text: {
                        if (importController.queueCount > 0) {
                            "Importing... (" + importController.queueCount + " queued)"
                        } else {
                            "No imports running"
                        }
                    }
                    color: themePalette.text
                    font.bold: true
                }

                // Line 2: progress bar
                Rectangle {
                    Layout.fillWidth: true
                    height: 20
                    color: themePalette.surface
                    radius: 2
                    border.color: themePalette.border
                    border.width: 1

                    Rectangle {
                        height: parent.height
                        width: parent.width * 0.5  // Placeholder: would be driven by controller
                        color: themePalette.accent
                        radius: 2
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "0/0"
                        color: themePalette.textDim
                        font.pixelSize: 10
                    }
                }
            }
        }

        // Queued list (placeholder)
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4

            Text {
                anchors.centerIn: parent
                text: "Queued items: " + importController.queueCount
                color: themePalette.textDim
            }
        }
    }

    // Lane failure banner (shown when lane_failed signal fires)
    Rectangle {
        id: footerBanner
        anchors.bottom: closeButton.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 0  // Hidden until needed
        color: themePalette.danger
        clip: true

        Text {
            anchors.fill: parent
            anchors.margins: 4
            text: "Import error"
            color: "white"
            wrapMode: Text.Wrap
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

    // Keyboard handling: Esc closes, Del/C handled by controller
    Keys.onEscapePressed: {
        root.visible = false
        root.back()
    }
    Keys.onDeletePressed: {
        // Del cancels selected task
        // Controller method: importController.cancel(taskId)
    }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_C && !event.isAutoRepeat) {
            // Clear finished: importController.clearFinished()
        }
    }

    focus: visible
}
