import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: "Export Warning"
    standardButtons: Dialog.Yes | Dialog.No
    modal: true

    property var themePalette

    ColumnLayout {
        width: 400
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            height: 60
            color: root.themePalette ? root.themePalette.danger : "#ffcccc"
            radius: 4

            Text {
                anchors.fill: parent
                anchors.margins: 8
                text: "WARNING: This will write decrypted data to disk"
                color: "white"
                font.bold: true
                wrapMode: Text.Wrap
            }
        }

        Text {
            Layout.fillWidth: true
            text: "Exported files will be stored in plaintext. Only selected items will be exported.\n\nContinue?"
            color: root.themePalette ? root.themePalette.text : "black"
            wrapMode: Text.Wrap
        }
    }

    onAccepted: {
        // Proceed with export
    }

    onRejected: {
        // Cancel export
    }
}
