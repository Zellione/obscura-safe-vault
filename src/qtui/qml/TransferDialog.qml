import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: "Move/Copy"
    standardButtons: Dialog.Ok | Dialog.Cancel
    modal: true

    property var themePalette
    property int selectedCount: 0
    property string operationMode: "move"  // "move" or "copy"

    ColumnLayout {
        width: 400
        spacing: 12

        // Mode selection
        GroupBox {
            title: "Operation"
            Layout.fillWidth: true

            ColumnLayout {
                RadioButton {
                    text: "Move"
                    checked: root.operationMode === "move"
                    onToggled: root.operationMode = "move"
                }
                RadioButton {
                    text: "Copy"
                    checked: root.operationMode === "copy"
                    onToggled: root.operationMode = "copy"
                }
            }
        }

        // Vault selection (placeholder)
        Text {
            text: "Select destination vault (with unlock if needed)"
            color: root.themePalette ? root.themePalette.text : "black"
        }

        // Gallery selection (placeholder)
        Text {
            text: "Select destination gallery"
            color: root.themePalette ? root.themePalette.text : "black"
        }

        Text {
            text: root.selectedCount + " item(s) selected"
            color: root.themePalette ? root.themePalette.textDim : "gray"
        }
    }

    onAccepted: {
        // Apply move/copy operation
    }
}
