import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: root
    color: themePalette.bg

    required property var themePalette
    required property var advancedSearchController
    required property var selectionController
    required property var viewerController
    required property var sessionState

    signal back()

    // Tab order for input fields
    property int focusField: 0  // 0=include, 1=exclude, 2=name, 3=scope
    property string includeText: ""
    property string excludeText: ""
    property string nameText: ""
    property int scopeIndex: 2  // 0=Images, 1=Galleries, 2=Both

    property var currentResults: []
    property bool listView: false  // false=grid, true=list
    property var savedSearchList: []  // Populated from controller
    property string savedSearchFilter: ""

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: themePalette.surfaceHi
            border.color: themePalette.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 16

                Text {
                    text: "Advanced Search"
                    color: themePalette.text
                    font.pixelSize: 14
                    font.bold: true
                    Layout.fillWidth: true
                }

                Text {
                    text: "Ctrl+R clear | Shift+/ new"
                    color: themePalette.textDim
                    font.pixelSize: 11
                }
            }
        }

        // Search controls
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                RowLayout {
                    spacing: 8
                    Text {
                        text: "Include:"
                        color: themePalette.text
                        font.pixelSize: 11
                        Layout.preferredWidth: 60
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        color: themePalette.surfaceHi
                        border.color: focusField === 0 ? themePalette.accent : themePalette.border
                        border.width: 1

                        TextInput {
                            id: includeInput
                            anchors.fill: parent
                            anchors.margins: 4
                            text: includeText
                            color: themePalette.text
                            font.pixelSize: 11
                            onTextChanged: { includeText = text; performSearch(); }
                            Keys.onTabPressed: { focusField = 1; includeInput.focus = false }
                            Keys.onBacktabPressed: { focusField = 3; includeInput.focus = false }
                        }
                    }
                }

                RowLayout {
                    spacing: 8
                    Text {
                        text: "Exclude:"
                        color: themePalette.text
                        font.pixelSize: 11
                        Layout.preferredWidth: 60
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        color: themePalette.surfaceHi
                        border.color: focusField === 1 ? themePalette.accent : themePalette.border
                        border.width: 1

                        TextInput {
                            id: excludeInput
                            anchors.fill: parent
                            anchors.margins: 4
                            text: excludeText
                            color: themePalette.text
                            font.pixelSize: 11
                            onTextChanged: { excludeText = text; performSearch(); }
                            Keys.onTabPressed: { focusField = 2; excludeInput.focus = false }
                            Keys.onBacktabPressed: { focusField = 0; excludeInput.focus = false }
                        }
                    }
                }

                RowLayout {
                    spacing: 8
                    Text {
                        text: "Name:"
                        color: themePalette.text
                        font.pixelSize: 11
                        Layout.preferredWidth: 60
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        color: themePalette.surfaceHi
                        border.color: focusField === 2 ? themePalette.accent : themePalette.border
                        border.width: 1

                        TextInput {
                            id: nameInput
                            anchors.fill: parent
                            anchors.margins: 4
                            text: nameText
                            color: themePalette.text
                            font.pixelSize: 11
                            onTextChanged: { nameText = text; performSearch(); }
                            Keys.onTabPressed: { focusField = 3; nameInput.focus = false }
                            Keys.onBacktabPressed: { focusField = 1; nameInput.focus = false }
                        }
                    }
                }

                RowLayout {
                    spacing: 8
                    Text {
                        text: "Scope:"
                        color: themePalette.text
                        font.pixelSize: 11
                        Layout.preferredWidth: 60
                    }
                    Text {
                        text: scopeIndex === 0 ? "Images" : scopeIndex === 1 ? "Galleries" : "Both"
                        color: themePalette.text
                        font.pixelSize: 11
                        Layout.fillWidth: true
                    }
                }
            }
        }

        // Results
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: themePalette.bg

            GridView {
                id: resultsGrid
                anchors.fill: parent
                anchors.margins: 8
                cellWidth: 128
                cellHeight: 128
                visible: !listView
                model: currentResults.length
                clip: true

                delegate: Rectangle {
                    width: resultsGrid.cellWidth - 4
                    height: resultsGrid.cellHeight - 4
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: modelData.is_gallery ? "📁" : "🖼"
                        font.pixelSize: 32
                    }

                    Text {
                        anchors {
                            bottom: parent.bottom
                            left: parent.left
                            right: parent.right
                            margins: 4
                        }
                        text: modelData.name
                        color: themePalette.text
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: selectionController.setSelected(modelData.path, true)
                        onDoubleClicked: openResult(modelData)
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }

            ListView {
                id: resultsList
                anchors.fill: parent
                anchors.margins: 8
                visible: listView
                model: currentResults.length
                clip: true

                delegate: Rectangle {
                    width: resultsList.width
                    height: 32
                    color: themePalette.surface
                    border.color: themePalette.border
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 8

                        Text {
                            text: modelData.is_gallery ? "📁" : "🖼"
                            font.pixelSize: 16
                        }

                        Text {
                            text: modelData.name
                            color: themePalette.text
                            font.pixelSize: 11
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: selectionController.setSelected(modelData.path, true)
                        onDoubleClicked: openResult(modelData)
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
            }

            Text {
                anchors.centerIn: parent
                text: currentResults.length === 0 ? "No results" : ""
                color: themePalette.textDim
                font.pixelSize: 14
                visible: currentResults.length === 0
            }
        }

        // Footer
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: themePalette.surfaceHi
            border.color: themePalette.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 16

                Text {
                    text: `${currentResults.length} results`
                    color: themePalette.text
                    font.pixelSize: 11
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "transparent"
                }

                Text {
                    text: "Ctrl+L list | Esc back"
                    color: themePalette.textDim
                    font.pixelSize: 10
                }
            }
        }
    }

    function performSearch() {
        const includes = includeText.split(/[\s,]+/).filter(t => t.length > 0);
        const excludes = excludeText.split(/[\s,]+/).filter(t => t.length > 0);
        advancedSearchController.search(includes, excludes, nameText, scopeIndex);
        // Session persistence: save query state
        sessionState.setCustomData("adv_search_query", JSON.stringify({
            include: includeText,
            exclude: excludeText,
            name: nameText,
            scope: scopeIndex,
            listView: listView
        }));
    }

    function restoreSessionState() {
        // Restore query from session state if available
        const saved = sessionState.getCustomData("adv_search_query");
        if (saved) {
            try {
                const data = JSON.parse(saved);
                includeText = data.include || "";
                excludeText = data.exclude || "";
                nameText = data.name || "";
                scopeIndex = data.scope || 2;
                listView = data.listView || false;
            } catch (e) {
                // Ignore parse errors, use defaults
            }
        }
    }

    function openResult(item) {
        if (item.is_gallery) {
            // Open gallery
        } else {
            // Open image in collection viewer (WS3 contract)
            const nodeKeys = currentResults.map(r => r.nodeKey);
            const index = currentResults.indexOf(item);
            viewerController.openAlbum(nodeKeys, index);
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            root.back();
            event.accepted = true;
        } else if (event.key === Qt.Key_Tab) {
            focusField = (focusField + 1) % 4;
            event.accepted = true;
        } else if (event.key === Qt.Key_L && (event.modifiers & Qt.ControlModifier)) {
            listView = !listView;
            event.accepted = true;
        } else if (event.key === Qt.Key_R && (event.modifiers & Qt.ControlModifier)) {
            includeText = "";
            excludeText = "";
            nameText = "";
            scopeIndex = 2;
            event.accepted = true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (currentResults.length > 0) {
                openResult(currentResults[0]);
            }
            event.accepted = true;
        } else if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier)) {
            // Ctrl+S: save current search with a name (for now, use timestamp)
            const name = "Search_" + new Date().getTime();
            advancedSearchController.saveSearch(name, includeText.split(/[\s,]+/).filter(t => t.length > 0),
                                               excludeText.split(/[\s,]+/).filter(t => t.length > 0),
                                               nameText, scopeIndex);
            advancedSearchController.refreshSavedSearches();
            event.accepted = true;
        } else if (event.key === Qt.Key_D && (event.modifiers & Qt.ControlModifier)) {
            // contract: WS2 DetailPanel — wired in integration
            // Ctrl+D: open detail panel (deferred to WS2)
            event.accepted = true;
        }
    }

    focus: true

    Component.onCompleted: {
        advancedSearchController.refreshTagVocabulary();
        advancedSearchController.refreshSavedSearches();
        restoreSessionState();
        performSearch();
    }

    Component.onDestruction: {
        // Persist session state on exit
        sessionState.setCustomData("adv_search_query", JSON.stringify({
            include: includeText,
            exclude: excludeText,
            name: nameText,
            scope: scopeIndex,
            listView: listView
        }));
    }

    Connections {
        target: advancedSearchController
        function onResultsChanged() {
            currentResults = advancedSearchController.results;
        }
        function onSavedSearchesChanged() {
            savedSearchList = advancedSearchController.savedSearches;
        }
    }
}
