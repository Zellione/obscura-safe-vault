import QtQuick
import QtQuick.Controls
import Osv 1.0

Dialog {
    id: dialog
    title: "Edit Tags"
    modal: true
    anchors.centerIn: parent

    property string nodePath: ""

    // Internal state
    property var ownTags: []
    property var inheritedTags: []
    property var contentsTags: []
    property string inputBuffer: ""
    property var suggestedTags: []

    signal closed()

    // Load tags when dialog opens
    function loadTags() {
        if (!nodePath) return
        ownTags = tagController.getOwnTags(nodePath)
        inheritedTags = tagController.getInheritedTags(nodePath)
        contentsTags = tagController.getContentsTags(nodePath)
        inputBuffer = ""
        suggestedTags = []
    }

    contentItem: Column {
        spacing: 12
        width: 500
        padding: 16

        // Own tags (editable)
        Text {
            text: "Tags"
            color: themePalette.text
            font.bold: true
            font.pixelSize: 13
        }

        Rectangle {
            width: parent.width
            height: Math.max(120, ownTagsColumn.implicitHeight + 12)
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4

            Column {
                id: ownTagsColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    leftMargin: 8
                    rightMargin: 8
                    topMargin: 8
                }
                spacing: 4

                Repeater {
                    model: dialog.ownTags.length
                    delegate: Rectangle {
                        width: parent.width
                        height: 28
                        color: themePalette.surfaceHi
                        border.color: themePalette.border
                        border.width: 1
                        radius: 3

                        Row {
                            anchors {
                                fill: parent
                                leftMargin: 8
                                rightMargin: 4
                            }
                            spacing: 8

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: tagController.getTagDisplayText(dialog.ownTags[index])
                                color: themePalette.text
                                font.pixelSize: 12
                            }

                            Item { Layout.fillWidth: true }

                            Button {
                                width: 20
                                height: 20
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Del"
                                font.pixelSize: 10

                                background: Rectangle {
                                    color: themePalette.danger
                                    radius: 3
                                }
                                contentItem: Text {
                                    text: parent.text
                                    color: themePalette.bg
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: 10
                                }

                                onClicked: {
                                    tagController.removeTag(dialog.nodePath, dialog.ownTags[index])
                                    dialog.loadTags()
                                }
                            }
                        }
                    }
                }
            }
        }

        // Add tag input
        Row {
            width: parent.width
            spacing: 8

            Rectangle {
                width: parent.width - addBtn.width - 8
                height: 36
                color: themePalette.surface
                border.color: themePalette.border
                border.width: 1
                radius: 4

                TextInput {
                    id: tagInput
                    anchors {
                        fill: parent
                        leftMargin: 8
                        rightMargin: 8
                    }
                    verticalAlignment: TextInput.AlignVCenter
                    text: dialog.inputBuffer
                    color: themePalette.text
                    font.pixelSize: 12
                    placeholderText: "Add tag..."
                    placeholderTextColor: themePalette.textDim

                    onTextChanged: {
                        dialog.inputBuffer = text
                        // Update suggestions
                        dialog.suggestedTags = tagController.getSuggestions(text, dialog.nodePath)
                    }

                    Keys.onReturnPressed: {
                        if (text.trim()) {
                            tagController.addTag(dialog.nodePath, text.trim())
                            dialog.loadTags()
                            text = ""
                        }
                    }
                }
            }

            Button {
                id: addBtn
                width: 60
                height: 36
                text: "Add"

                background: Rectangle {
                    color: themePalette.accent
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: themePalette.bg
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 12
                }

                onClicked: {
                    if (tagInput.text.trim()) {
                        tagController.addTag(dialog.nodePath, tagInput.text.trim())
                        dialog.loadTags()
                        tagInput.text = ""
                    }
                }
            }
        }

        // Autosuggest dropdown (simplified - just show list)
        Rectangle {
            width: parent.width
            height: Math.min(suggestListView.contentHeight, 100)
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4
            visible: dialog.suggestedTags.length > 0

            ListView {
                id: suggestListView
                anchors {
                    fill: parent
                    margins: 1
                }
                model: dialog.suggestedTags
                spacing: 1
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    width: suggestListView.width
                    height: 28
                    color: ListView.isCurrentItem ? themePalette.surfaceHi : themePalette.surface

                    Text {
                        anchors {
                            verticalCenter: parent.verticalCenter
                            left: parent.left
                            leftMargin: 8
                        }
                        text: modelData
                        color: themePalette.text
                        font.pixelSize: 12
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            tagController.addTag(dialog.nodePath, modelData)
                            dialog.loadTags()
                        }
                    }
                }
            }
        }

        // Inherited tags (read-only)
        Text {
            text: "Inherited"
            color: themePalette.textDim
            font.italic: true
            font.pixelSize: 12
            visible: dialog.inheritedTags.length > 0
        }

        Rectangle {
            width: parent.width
            height: inheritedColumn.implicitHeight + 8
            color: "transparent"
            visible: dialog.inheritedTags.length > 0

            Column {
                id: inheritedColumn
                anchors {
                    fill: parent
                    margins: 4
                }
                spacing: 4

                Repeater {
                    model: dialog.inheritedTags.length
                    delegate: Text {
                        text: tagController.getTagDisplayText(dialog.inheritedTags[index])
                        color: themePalette.textDim
                        font.pixelSize: 11
                    }
                }
            }
        }

        // Contents tags (read-only)
        Text {
            text: "From Contents"
            color: themePalette.textDim
            font.italic: true
            font.pixelSize: 12
            visible: dialog.contentsTags.length > 0
        }

        Rectangle {
            width: parent.width
            height: contentsColumn.implicitHeight + 8
            color: "transparent"
            visible: dialog.contentsTags.length > 0

            Column {
                id: contentsColumn
                anchors {
                    fill: parent
                    margins: 4
                }
                spacing: 4

                Repeater {
                    model: dialog.contentsTags.length
                    delegate: Text {
                        text: tagController.getTagDisplayText(dialog.contentsTags[index])
                        color: themePalette.textDim
                        font.pixelSize: 11
                    }
                }
            }
        }
    }

    standardButtons: Dialog.Ok | Dialog.Cancel

    onAccepted: {
        dialog.closed()
    }

    onRejected: {
        dialog.closed()
    }

    onOpened: {
        dialog.loadTags()
        tagInput.focus = true
    }
}
