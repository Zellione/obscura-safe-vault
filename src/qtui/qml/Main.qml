import QtQuick
import Osv 1.0

Window {
    id: root
    width: 1280
    height: 800
    visible: true
    color: "#14161a"          // placeholder until ThemePalette (Task 8)
    title: "osv-qt (experiment)"

    StackView {
        id: stack
        anchors.fill: parent

        initialItem: UnlockScreen {}

        Connections {
            target: unlockController
            function onUnlockedChanged() {
                if (unlockController.unlocked) {
                    stack.replace(unlockedPage)
                } else {
                    stack.replace(unlockScreen)
                }
            }
        }

        Component {
            id: unlockScreen
            UnlockScreen {}
        }

        Component {
            id: unlockedPage
            Rectangle {
                color: "#14161a"
                SecureImageItem {
                    anchors.fill: parent
                    Component.onCompleted: unlockController.loadFirstImage(this)
                }
            }
        }
    }
}
