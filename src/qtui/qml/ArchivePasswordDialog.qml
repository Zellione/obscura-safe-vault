import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as QmlUI

Dialog {
    id: root
    title: "Archive Password"
    standardButtons: Dialog.Ok | Dialog.Cancel

    required property var themePalette
    required property var importController

    property string archiveName: ""

    ColumnLayout {
        width: 300
        spacing: 8

        Text {
            text: "Enter password for " + root.archiveName
            color: root.themePalette.text
            wrapMode: Text.Wrap
        }

        QmlUI.SecureTextField {
            id: passwordField
            Layout.fillWidth: true
            Layout.preferredHeight: 32
        }

        Text {
            text: "Password: " + passwordField.length + " characters"
            color: root.themePalette.textDim
            font.pixelSize: 11
        }
    }

    onAccepted: {
        // Pass SecurePassword (SecureTextInput bytes) to controller
        // Controller will handle wrapping as shared_ptr<SecureBytes>
        importController.submitArchivePassword(archiveName, passwordField.model)
        passwordField.clearSecret()
        root.close()
    }

    onRejected: {
        // Clear on cancel
        passwordField.clearSecret()
        root.close()
    }
}
