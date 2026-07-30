import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: "Archive Password"
    standardButtons: Dialog.Ok | Dialog.Cancel

    property string archiveName: ""

    ColumnLayout {
        width: 300
        spacing: 8

        Text {
            text: "Enter password for " + root.archiveName
            color: "black"
            wrapMode: Text.Wrap
        }

        TextField {
            id: passwordField
            Layout.fillWidth: true
            echoMode: TextInput.Password
            placeholderText: "Password"
        }
    }

    onAccepted: {
        // Return password via signal or property
        // Full impl: pass SecurePassword to controller
    }
}
