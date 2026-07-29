import QtQuick
import Osv 1.0

Window {
    id: root
    width: 400
    height: 400
    visible: true
    color: "#000000"
    title: "Renderer Selftest"

    SecureImageItem {
        id: testItem
        anchors.fill: parent
    }

    Component.onCompleted: {
        // Notify C++ to set the test image
        // This is done via a test-only property
        root.objectName = "testWindow"
        testItem.objectName = "testItem"
    }
}
