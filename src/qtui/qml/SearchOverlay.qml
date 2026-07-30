import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root
    visible: active
    color: themePalette.veil
    z: 1000

    required property var themePalette
    required property var searchModelAdapter
    required property var viewerController
    required property var galleryModel

    signal back()
    signal openGallery(nodePath: string)
    signal openViewer(nodeKey: int)

    property bool active: false
    property int currentScope: 0  // 0=Both, 1=Images, 2=Galleries
    property var searchResults: []
    property int selectedIndex: 0

    // SearchScope enum from vault (via C++)
    readonly property int ScopeBoth: 0
    readonly property int ScopeImages: 1
    readonly property int ScopeGalleries: 2

    function open() {
        active = true;
        inputField.focus = true;
        inputField.selectAll();
    }

    function close() {
        active = false;
        currentScope = ScopeBoth;
        inputField.text = "";
        selectedIndex = 0;
    }

    function cycleScopee() {
        currentScope = (currentScope + 1) % 3;
        updateSearch();
    }

    function updateSearch() {
        if (!active) return;
        let query = inputField.text;
        searchResults = searchModelAdapter.search(query, currentScope);
        selectedIndex = 0;
    }

    function selectPrevious() {
        if (selectedIndex > 0) {
            selectedIndex--;
            resultsView.positionViewAtIndex(selectedIndex, ListView.Contain);
        }
    }

    function selectNext() {
        if (selectedIndex < searchResults.length - 1) {
            selectedIndex++;
            resultsView.positionViewAtIndex(selectedIndex, ListView.Contain);
        }
    }

    function activateSelected() {
        if (selectedIndex < 0 || selectedIndex >= searchResults.length) {
            return;
        }

        let item = searchResults[selectedIndex];
        if (item.is_gallery) {
            root.openGallery(item.path);
        } else {
            // Open image in collection-mode viewer (WS3 contract)
            let nodeKeys = searchResults.map(it => it.nodeKey || 0);
            viewerController.openAlbum(nodeKeys, selectedIndex);
        }
        close();
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

        // Search input with scope indicator
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                Text {
                    text: "/"
                    color: themePalette.textDim
                    font.pixelSize: 14
                }

                TextField {
                    id: inputField
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    placeholderText: "Search images, galleries... (Tab = scope, Esc = close)"
                    background: Rectangle { color: "transparent" }
                    color: themePalette.text
                    placeholderTextColor: themePalette.textFaint
                    verticalAlignment: TextInput.AlignVCenter
                    font.pixelSize: 14

                    onTextChanged: root.updateSearch()
                    onAccepted: root.activateSelected()

                    Keys.onEscapePressed: root.close()
                    Keys.onTabPressed: root.cycleScopee()
                    Keys.onUpPressed: root.selectPrevious()
                    Keys.onDownPressed: root.selectNext()
                    Keys.onReturnPressed: root.activateSelected()
                }

                Text {
                    text: {
                        switch (root.currentScope) {
                        case ScopeBoth: return "Both";
                        case ScopeImages: return "Images";
                        case ScopeGalleries: return "Galleries";
                        default: return "Both";
                        }
                    }
                    color: themePalette.textDim
                    font.pixelSize: 12
                    width: 80
                    horizontalAlignment: Text.AlignRight
                }
            }
        }

        // Results list
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1
            radius: 4
            clip: true

            ListView {
                id: resultsView
                anchors.fill: parent
                anchors.margins: 4
                model: root.searchResults
                spacing: 4

                delegate: Rectangle {
                    width: resultsView.width - 8
                    height: 48
                    color: (index === root.selectedIndex)
                        ? themePalette.surfaceHi
                        : themePalette.surface
                    border.color: (index === root.selectedIndex)
                        ? themePalette.accent
                        : "transparent"
                    border.width: 2
                    radius: 3

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        Text {
                            text: modelData.is_gallery ? "📁" : "🖼"
                            font.pixelSize: 24
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: modelData.name
                                color: themePalette.text
                                font.pixelSize: 13
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            Text {
                                text: modelData.is_gallery ? "Gallery" : "Image"
                                color: themePalette.textDim
                                font.pixelSize: 11
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.selectedIndex = index;
                        }
                        onDoubleClicked: {
                            root.selectedIndex = index;
                            root.activateSelected();
                        }
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                text: "No results"
                color: themePalette.textDim
                font.pixelSize: 14
                visible: root.searchResults.length === 0
            }
        }

        // Status bar
        Text {
            Layout.fillWidth: true
            text: root.searchResults.length > 0
                ? `${selectedIndex + 1} of ${searchResults.length} results`
                : "No results"
            color: themePalette.textDim
            font.pixelSize: 12
        }
    }

    Keys.onEscapePressed: root.close()
    Keys.priority: Keys.BeforeItem
    focus: active
}
