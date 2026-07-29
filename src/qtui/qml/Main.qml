import QtQuick

Window {
    id: root
    width: 1280
    height: 800
    visible: true
    color: "#14161a"
    title: "osv-qt (experiment)"

    Rectangle {
        anchors.fill: parent
        color: "#14161a"

        Text {
            anchors.centerIn: parent
            text: unlockController.unlocked ? "Unlocked" : "Locked"
            color: "#c8ccd4"
            font.pixelSize: 24
        }
    }
}
