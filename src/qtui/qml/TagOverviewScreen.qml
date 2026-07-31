import QtQuick

Rectangle {
    id: screen
    color: themePalette.bg

    property string screenName: "TagOverview"
    property var controller: tagOverviewController  // Set by app wiring
    property var selectionController
    property var viewerController
    property var statusController

    // Navigation signals
    signal back()
    signal openTagGalleries(string tag)
    signal openTagImages(string tag)

    // Help groups (deferred features E, Ctrl+I, Ctrl+L omitted per standing rule)
    property list<var> helpGroups: [
        { title: "Navigation", entries: [
            { key: "Up/Down", text: "Browse tags" },
            { key: "Enter", text: "View tag items (toggle with Tab)" },
            { key: "Tab", text: "Toggle: galleries ↔ images" },
            { key: "Esc", text: "Return to gallery" }
        ]},
        { title: "Filtering", entries: [
            { key: "/", text: "Enter filter mode" },
            { key: "Ctrl+R", text: "Clear filter" },
            { key: "Esc (empty)", text: "Exit filter mode" }
        ]}
    ]

    // Tag overview state
    enum ViewMode { Galleries, Images }
    enum SortMode { Name, Count }

    property int viewMode: TagOverviewScreen.ViewMode.Galleries
    property int sortMode: TagOverviewScreen.SortMode.Name
    property int selectedIndex: 0
    property string filterText: ""
    property bool inFilterMode: false
    property string editingTag: ""
    property string editingDescription: ""
    property bool showImportSummary: false
    property var importSummaryLines: []
    property string importError: ""

    // Ensure controller is wired
    Component.onCompleted: {
        controller.refresh()
    }

    // Layout
    implicitWidth: 800
    implicitHeight: 600

    // Header band with title
    Rectangle {
        id: headerBand
        width: parent.width
        height: 40
        color: themePalette.stripBg

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: themePalette.border
        }

        Text {
            anchors.centerIn: parent
            text: inFilterMode ? "Filter tags: " + filterText : "Tags (" + controller.tags.length + ")"
            color: themePalette.text
            font.pixelSize: 14
            font.bold: true
        }
    }

    // Main content area: tag list
    Rectangle {
        anchors.top: headerBand.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: footerBand.top
        color: themePalette.bg

        ListView {
            id: tagList
            anchors.fill: parent
            model: controller.tags
            currentIndex: selectedIndex
            clip: true

            delegate: Rectangle {
                id: tagDelegate
                width: tagList.width
                height: 60
                color: tagList.currentIndex === index ? themePalette.surfaceHi : themePalette.surface
                border {
                    color: tagList.currentIndex === index ? themePalette.accent : "transparent"
                    width: tagList.currentIndex === index ? 2 : 0
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.topMargin: 4
                    anchors.bottomMargin: 4
                    spacing: 2

                    // Line 1: Tag chip + Counts
                    Row {
                        spacing: 8
                        height: 24

                        // Tag chip (simplified - just colored text)
                        Rectangle {
                            width: 12
                            height: 12
                            radius: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: themePalette.accent
                        }

                        Text {
                            text: modelData.tag
                            color: themePalette.text
                            font.pixelSize: 12
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            text: "(" + modelData.galleryCount + " galleries · " + modelData.imageCount + " images)"
                            color: themePalette.textDim
                            font.pixelSize: 10
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }

                    // Line 2: Description or edit prompt
                    Text {
                        text: modelData.description ? modelData.description : "(no description — [E] to add)"
                        color: modelData.description ? themePalette.text : themePalette.textFaint
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        width: parent.width
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        tagList.currentIndex = index
                        selectedIndex = index
                    }
                    onDoubleClicked: {
                        if (viewMode === TagOverviewScreen.ViewMode.Galleries) {
                            openTagGalleries(modelData.tag)
                        } else {
                            openTagImages(modelData.tag)
                        }
                    }
                }
            }
        }
    }

    // Footer band with status and hints
    Rectangle {
        id: footerBand
        anchors.bottom: parent.bottom
        width: parent.width
        height: 48
        color: themePalette.stripBg

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: themePalette.border
        }

        Row {
            anchors.centerIn: parent
            spacing: 16

            Text {
                text: "[Tab] " + (viewMode === TagOverviewScreen.ViewMode.Galleries ? "Galleries" : "Images")
                color: themePalette.text
                font.pixelSize: 11
            }

            Text {
                text: "[E] Edit"
                color: themePalette.text
                font.pixelSize: 11
            }

            Text {
                text: "[/] Filter"
                color: themePalette.text
                font.pixelSize: 11
            }

            Text {
                text: "[Ctrl+I] Import"
                color: themePalette.text
                font.pixelSize: 11
            }

            Text {
                text: inFilterMode ? "[Esc] Exit filter" : "[Esc] Close"
                color: themePalette.warn
                font.pixelSize: 11
            }
        }
    }

    // Import summary modal
    Rectangle {
        id: importModal
        anchors.fill: parent
        color: themePalette.veil
        visible: showImportSummary

        Rectangle {
            anchors.centerIn: parent
            width: 400
            height: Math.min(300, importSummaryLines.length * 20 + 80)
            radius: 4
            color: themePalette.surface
            border {
                color: themePalette.border
                width: 1
            }

            Column {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8

                Text {
                    text: "Import Summary"
                    color: themePalette.text
                    font.pixelSize: 14
                    font.bold: true
                }

                Rectangle {
                    width: parent.width
                    height: 1
                    color: themePalette.border
                }

                ListView {
                    width: parent.width
                    height: parent.height - 60
                    model: importSummaryLines
                    delegate: Text {
                        text: modelData
                        color: themePalette.text
                        font.pixelSize: 10
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }
                    clip: true
                }

                Text {
                    text: "(Press any key to dismiss)"
                    color: themePalette.textFaint
                    font.pixelSize: 9
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }
            }

            MouseArea {
                anchors.fill: parent
                onPressed: {
                    showImportSummary = false
                    controller.clearImportSummary()
                }
            }

            Keys.onPressed: {
                showImportSummary = false
                controller.clearImportSummary()
            }
        }
    }

    // Keyboard navigation
    Keys.onPressed: (event) => {
        if (showImportSummary) {
            showImportSummary = false
            controller.clearImportSummary()
            event.accepted = true
            return
        }

        if (inFilterMode) {
            if (event.key === Qt.Key_Escape || (event.key === Qt.Key_Backspace && filterText.length === 0)) {
                inFilterMode = false
                filterText = ""
                controller.filterByPrefix("")
                event.accepted = true
            } else if (event.key === Qt.Key_Backspace) {
                filterText = filterText.slice(0, -1)
                controller.filterByPrefix(filterText)
                event.accepted = true
            } else if (event.text && event.text !== " ") {
                filterText += event.text
                controller.filterByPrefix(filterText)
                event.accepted = true
            }
            return
        }

        switch (event.key) {
        case Qt.Key_Up:
            if (selectedIndex > 0) {
                selectedIndex--
                tagList.currentIndex = selectedIndex
            }
            event.accepted = true
            break
        case Qt.Key_Down:
            if (selectedIndex < controller.tags.length - 1) {
                selectedIndex++
                tagList.currentIndex = selectedIndex
            }
            event.accepted = true
            break
        case Qt.Key_Tab:
            viewMode = viewMode === TagOverviewScreen.ViewMode.Galleries
                ? TagOverviewScreen.ViewMode.Images
                : TagOverviewScreen.ViewMode.Galleries
            event.accepted = true
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            if (selectedIndex >= 0 && selectedIndex < controller.tags.length) {
                let tag = controller.tags[selectedIndex].tag
                if (viewMode === TagOverviewScreen.ViewMode.Galleries) {
                    openTagGalleries(tag)
                } else {
                    openTagImages(tag)
                }
            }
            event.accepted = true
            break
        case Qt.Key_E:
            if (selectedIndex >= 0 && selectedIndex < controller.tags.length) {
                editingTag = controller.tags[selectedIndex].tag
                editingDescription = controller.tags[selectedIndex].description
                // TODO: Show inline edit dialog
            }
            event.accepted = true
            break
        case Qt.Key_Slash:
            if (!event.modifiers) {
                inFilterMode = true
                filterText = ""
                event.accepted = true
            }
            break
        case Qt.Key_R:
            if (event.modifiers & Qt.ControlModifier) {
                filterText = ""
                inFilterMode = false
                controller.filterByPrefix("")
                event.accepted = true
            }
            break
        case Qt.Key_L:
            if (event.modifiers & Qt.ControlModifier) {
                // TODO: Toggle list view
                event.accepted = true
            }
            break
        case Qt.Key_I:
            if (event.modifiers & Qt.ControlModifier) {
                // TODO: Open file dialog for JSON import
                // For now, just show a placeholder
                importSummaryLines = ["JSON import not yet wired (FileDialog needed)"]
                showImportSummary = true
                event.accepted = true
            }
            break
        case Qt.Key_Escape:
            back()
            event.accepted = true
            break
        }
    }

    focus: true
}
