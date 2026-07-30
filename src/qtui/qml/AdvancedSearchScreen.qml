import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "." as Local

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
    property int focusField: 0  // 0=include, 1=exclude, 2=name, 3=scope, 4=sidebar
    property string includeText: ""
    property string excludeText: ""
    property string nameText: ""
    property int scopeIndex: 2  // 0=Images, 1=Galleries, 2=Both

    property var currentResults: []
    property bool listView: false  // false=grid, true=list
    property var savedSearchList: []  // Populated from controller
    property string savedSearchFilter: ""
    property int savedSearchFocusIndex: -1  // Currently focused saved search
    property bool sidebarFilterMode: false  // `/` enters filter mode

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Left sidebar: Saved searches
        Rectangle {
            Layout.preferredWidth: 180
            Layout.fillHeight: true
            color: themePalette.surface
            border.color: themePalette.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                // Sidebar header
                Text {
                    text: "Saved Searches"
                    color: themePalette.text
                    font.pixelSize: 12
                    font.bold: true
                    Layout.fillWidth: true
                }

                // Sidebar filter input (shown in filter mode)
                Rectangle {
                    visible: sidebarFilterMode
                    Layout.fillWidth: true
                    Layout.preferredHeight: 24
                    color: themePalette.surfaceHi
                    border.color: themePalette.accent
                    border.width: 1

                    TextInput {
                        id: sidebarFilterInput
                        anchors.fill: parent
                        anchors.margins: 4
                        text: savedSearchFilter
                        color: themePalette.text
                        font.pixelSize: 11
                        onTextChanged: { savedSearchFilter = text; }
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Backspace && text.length === 0) {
                                sidebarFilterMode = false;
                                savedSearchFilter = "";
                                event.accepted = true;
                            }
                        }
                    }
                }

                // Filtered saved searches list
                ListView {
                    id: savedSearchesList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: {
                        if (sidebarFilterMode && savedSearchFilter) {
                            return savedSearchList.filter(s => s.name.toLowerCase().includes(savedSearchFilter.toLowerCase()));
                        }
                        return savedSearchList;
                    }

                    delegate: Rectangle {
                        width: savedSearchesList.width
                        height: 28
                        color: savedSearchFocusIndex === index ? themePalette.accent : themePalette.surface
                        border.color: themePalette.border
                        border.width: 1

                        Text {
                            anchors {
                                left: parent.left
                                top: parent.top
                                bottom: parent.bottom
                                leftMargin: 4
                            }
                            verticalAlignment: Text.AlignVCenter
                            text: modelData.name
                            color: savedSearchFocusIndex === index ? themePalette.bg : themePalette.text
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: { savedSearchFocusIndex = index; }
                            onDoubleClicked: loadSavedSearch(index)
                        }
                    }

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                    }
                }

                // Empty state
                Text {
                    visible: savedSearchList.length === 0
                    text: "No saved searches"
                    color: themePalette.textDim
                    font.pixelSize: 10
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                // Sidebar footer hints
                Text {
                    text: "Enter:load | Del:remove | /:filter"
                    color: themePalette.textDim
                    font.pixelSize: 9
                    Layout.fillWidth: true
                }
            }
        }

        // Right side: Main content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
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
                                Keys.onBacktabPressed: { focusField = 4; includeInput.focus = false }
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
    }

    function performSearch() {
        const includes = includeText.split(/[\s,]+/).filter(t => t.length > 0);
        const excludes = excludeText.split(/[\s,]+/).filter(t => t.length > 0);
        advancedSearchController.search(includes, excludes, nameText, scopeIndex);
        // Session persistence: save query state
        sessionState.recordCustomData("adv_search_query", JSON.stringify({
            include: includeText,
            exclude: excludeText,
            name: nameText,
            scope: scopeIndex,
            listView: listView
        }));
    }

    function restoreSessionState() {
        // Restore query from session state if available
        const saved = sessionState.recallCustomData("adv_search_query");
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

    function loadSavedSearch(index) {
        if (index >= 0 && index < savedSearchList.length) {
            const search = savedSearchList[index];
            includeText = search.include.join(" ");
            excludeText = search.exclude.join(" ");
            nameText = search.name || "";
            scopeIndex = search.scope || 2;
            performSearch();
        }
    }

    Keys.onPressed: (event) => {
        // Handle sidebar-specific keys when in sidebar
        if (focusField === 4) {
            if (event.key === Qt.Key_Escape) {
                focusField = 0;
                event.accepted = true;
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (savedSearchFocusIndex >= 0) {
                    loadSavedSearch(savedSearchFocusIndex);
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_Delete) {
                if (savedSearchFocusIndex >= 0 && savedSearchList.length > savedSearchFocusIndex) {
                    const search = savedSearchList[savedSearchFocusIndex];
                    advancedSearchController.deleteSavedSearch(search.name);
                    advancedSearchController.refreshSavedSearches();
                }
                event.accepted = true;
            } else if (event.key === Qt.Key_Slash) {
                sidebarFilterMode = true;
                if (sidebarFilterInput) { sidebarFilterInput.focus = true; }
                event.accepted = true;
            } else if (event.key === Qt.Key_Up) {
                if (savedSearchFocusIndex > 0) { savedSearchFocusIndex--; }
                event.accepted = true;
            } else if (event.key === Qt.Key_Down) {
                if (savedSearchFocusIndex < savedSearchList.length - 1) { savedSearchFocusIndex++; }
                event.accepted = true;
            }
        } else if (event.key === Qt.Key_Escape) {
            root.back();
            event.accepted = true;
        } else if (event.key === Qt.Key_Tab) {
            focusField = (focusField + 1) % 5;
            if (focusField === 4 && savedSearchList.length > 0 && savedSearchFocusIndex < 0) {
                savedSearchFocusIndex = 0;
            }
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
            // Ctrl+S: open dialog to save with a user-provided name
            saveSearchDialog.existingNames = savedSearchList.map(s => s.name);
            saveSearchDialog.open();
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
        sessionState.recordCustomData("adv_search_query", JSON.stringify({
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

    // Save search dialog
    Local.SaveSearchDialog {
        id: saveSearchDialog
        themePalette: root.themePalette
        advancedSearchController: root.advancedSearchController

        onSaved: {
            // User confirmed save; perform the actual save
            const name = saveSearchDialog.getSearchName();
            advancedSearchController.saveSearch(name,
                                               includeText.split(/[\s,]+/).filter(t => t.length > 0),
                                               excludeText.split(/[\s,]+/).filter(t => t.length > 0),
                                               nameText, scopeIndex);
            advancedSearchController.refreshSavedSearches();
        }
    }
}
