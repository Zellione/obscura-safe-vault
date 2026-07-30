import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: "Volume Set"
    standardButtons: Dialog.Ok | Dialog.Cancel

    property var volumeSet: null
    property bool allVolumesPresent: true
    property var themePalette

    ColumnLayout {
        width: 400
        spacing: 8

        Text {
            text: "Missing volumes detected - this set cannot be imported"
            color: root.themePalette ? root.themePalette.danger : "red"
            visible: !root.allVolumesPresent
            wrapMode: Text.Wrap
        }

        Text {
            text: "Archive: " + (root.volumeSet ? root.volumeSet.stem : "")
            color: "black"
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 200

            ListView {
                model: root.volumeSet ? root.volumeSet.volumes : 0
                delegate: Text {
                    text: (index + 1) + ". " + modelData
                    color: "black"
                }
            }
        }

        Text {
            text: "… and " + Math.max(0, (root.volumeSet ? root.volumeSet.volumes.length : 0) - 8) + " more"
            visible: root.volumeSet && root.volumeSet.volumes.length > 8
            color: "gray"
        }
    }

    onAccepted: {
        if (root.allVolumesPresent) {
            // Proceed with import
        }
    }
}
