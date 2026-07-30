import QtQuick

Rectangle {
    // Footer status bar: opaque band with hairline border, themePalette.bg background
    // Displays statusController.text colored by message kind priority: error (danger) > import (text) > normal (textDim)
    // Text and color bindings are reactive (re-evaluate when statusController properties change)

    color: themePalette.bg
    height: 32

    // Helper function to resolve text color based on message kind priority
    function getTextColor() {
        switch (statusController.kind) {
        case 0:  // Normal
            return themePalette.textDim
        case 1:  // Import
            return themePalette.text
        case 2:  // Error
            return themePalette.danger
        default:
            return themePalette.textDim
        }
    }

    // Top border (hairline, 1px)
    Rectangle {
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 1
        color: themePalette.border
    }

    // Status text — reactive bindings (text and color re-evaluate on statusController changes)
    Text {
        anchors {
            verticalCenter: parent.verticalCenter
            left: parent.left
            leftMargin: 8
            right: parent.right
            rightMargin: 8
        }
        text: statusController.text
        color: getTextColor()
        font.pixelSize: 12
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
