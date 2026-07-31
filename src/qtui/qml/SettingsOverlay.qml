import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: settingsOverlay
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0)  // Transparent initially
    visible: false
    z: 1000  // Above other content

    // settingsController comes from the engine context property (a shadowing
    // property declaration made the old `settingsController: settingsController`
    // shell binding self-refer and left it null — T3.1 W5).

    // Internal state
    property bool isOpen: false
    property bool inPane: false  // false: rail focus, true: pane focus
    property int currentSection: 0  // 0=Appearance, 1=Browsing, 2=TagColours
    property int currentRow: 0  // focused row within section
    property bool renamingCategory: false  // inline rename mode
    property int renamingRow: -1  // which category is being renamed
    property string renamingText: ""  // current rename text

    // Signal emitted when overlay closes to allow focus restoration
    signal closed()

    // Calculate row count for current section (matches settings_model logic)
    function rowCountForSection(section) {
        if (section === 0) return 1;  // Appearance: theme
        if (section === 1) {
            // Browsing: base 2 rows (sort order, show tags) + 2 WS1 autolock rows if unlocked
            return settingsController && settingsController.vaultUnlocked ? 4 : 0;
        }
        if (section === 2) {
            // TagColours: category count if unlocked, 0 if locked
            return (settingsController && settingsController.vaultUnlocked)
                   ? (settingsController.categories ? settingsController.categories.length : 0)
                   : 0;
        }
        return 0;
    }

    // Called from Main.qml
    function toggle() {
        if (isOpen) {
            close();
        } else {
            open();
        }
    }

    function open() {
        if (isOpen) return;
        isOpen = true;
        visible = true;
        veilRect.opacity = 0.5;
        contentPanel.opacity = 1.0;
        inPane = false;
        currentRow = 0;
        contentPanel.forceActiveFocus();
    }

    function close() {
        if (!isOpen) return;
        isOpen = false;
        visible = false;
        veilRect.opacity = 0;
        contentPanel.opacity = 0;
        closed();  // Emit signal to restore focus
    }

    // Handle Up/Down navigation
    function moveRow(delta) {
        let maxRow = rowCountForSection(currentSection) - 1;
        if (maxRow < 0) maxRow = 0;
        currentRow = Math.max(0, Math.min(currentRow + delta, maxRow));
    }

    // Handle Left/Right value changes
    function changeValue(delta) {
        if (!settingsController || !inPane) return;

        // Appearance section
        if (currentSection === 0 && currentRow === 0) {
            let idx = settingsController.currentThemeIndex;
            idx = (idx + delta) % 4;
            if (idx < 0) idx += 4;
            settingsController.currentThemeIndex = idx;
        }
        // Browsing section: only if unlocked
        else if (currentSection === 1 && settingsController.vaultUnlocked) {
            if (currentRow === 0) {
                // Cycle sort order
                let idx = settingsController.currentSortOrderIndex;
                idx = (idx + delta) % settingsController.sortOrderList.length;
                if (idx < 0) idx += settingsController.sortOrderList.length;
                settingsController.currentSortOrderIndex = idx;
            } else if (currentRow === 1) {
                // Toggle tiles show tags
                settingsController.tilesShowTags = !settingsController.tilesShowTags;
            } else if (currentRow === 2 && autoLock) {
                // Change auto-lock idle seconds (delta of ±60 seconds, clamped to 30-3600)
                let newVal = autoLock.idleSeconds + (delta * 60);
                newVal = Math.max(30, Math.min(newVal, 3600));
                autoLock.idleSeconds = newVal;
            } else if (currentRow === 3 && autoLock) {
                // Toggle keep unlocked
                autoLock.keepUnlocked = !autoLock.keepUnlocked;
            }
        }
        // TagColours section: only if unlocked
        else if (currentSection === 2 && settingsController.vaultUnlocked) {
            // Placeholder: would handle category swatch cycling
        }
    }

    // Veil (semi-transparent overlay)
    Rectangle {
        id: veilRect
        anchors.fill: parent
        color: themePalette.veil
        opacity: 0
        z: -1

        MouseArea {
            anchors.fill: parent
            onClicked: settingsOverlay.close()
        }
    }

    // Content panel (veil + rail + pane)
    Rectangle {
        id: contentPanel
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, 1000)
        height: Math.min(parent.height * 0.85, 700)
        color: themePalette.surface
        border.color: themePalette.border
        border.width: 1
        radius: 8
        opacity: 0
        z: 1
        focus: true

        Behavior on opacity {
            NumberAnimation { duration: 150 }
        }

        // Header
        Rectangle {
            id: header
            anchors { top: parent.top; left: parent.left; right: parent.right }
            height: 40
            color: themePalette.surfaceHi
            radius: 8

            // Bottom border line
            Rectangle {
                anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                height: 1
                color: themePalette.border
            }

            Text {
                anchors {
                    left: parent.left
                    verticalCenter: parent.verticalCenter
                    leftMargin: 16
                }
                text: "F2 Settings"
                color: themePalette.text
                font.pixelSize: 14
                font.bold: true
            }

            Text {
                anchors {
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    rightMargin: 16
                }
                text: "(Esc to close)"
                color: themePalette.textDim
                font.pixelSize: 11
            }
        }

        // Body: horizontal layout (rail + pane)
        RowLayout {
            anchors {
                top: header.bottom
                left: parent.left
                right: parent.right
                bottom: footerLine.top
            }
            spacing: 0

            // Left rail: section selector
            Rectangle {
                Layout.preferredWidth: 160
                Layout.fillHeight: true
                color: themePalette.bg

                // Right border
                Rectangle {
                    anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                    width: 1
                    color: themePalette.border
                }

                Column {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                        topMargin: 8
                    }
                    spacing: 4

                    Repeater {
                        model: ["Appearance", "Browsing", "Tag Colours"]

                        Rectangle {
                            width: parent.width - 8
                            height: 36
                            anchors.horizontalCenter: parent.horizontalCenter
                            color: (index === settingsOverlay.currentSection && !settingsOverlay.inPane)
                                   ? themePalette.accent
                                   : (index === settingsOverlay.currentSection ? themePalette.surfaceHi : "transparent")
                            border.color: (index === settingsOverlay.currentSection && !settingsOverlay.inPane)
                                         ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Text {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 12
                                }
                                text: modelData
                                color: (index === settingsOverlay.currentSection && !settingsOverlay.inPane)
                                       ? themePalette.bg : themePalette.text
                                font.pixelSize: 12
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    settingsOverlay.currentSection = index;
                                    settingsOverlay.inPane = false;
                                    settingsOverlay.currentRow = 0;
                                    contentPanel.forceActiveFocus();
                                }
                            }
                        }
                    }
                }
            }

            // Right pane: content
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: themePalette.surface

                // Appearance section
                Column {
                    anchors {
                        fill: parent
                        margins: 16
                    }
                    spacing: 12
                    visible: settingsOverlay.currentSection === 0

                    Text {
                        text: "Theme"
                        color: themePalette.text
                        font.pixelSize: 12
                        font.bold: true
                    }

                    // Theme row (row 0)
                    Rectangle {
                        width: parent.width - 16
                        height: 40
                        color: (settingsOverlay.inPane && settingsOverlay.currentRow === 0)
                               ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2)
                               : "transparent"
                        border.color: (settingsOverlay.inPane && settingsOverlay.currentRow === 0)
                                     ? themePalette.accent : "transparent"
                        border.width: 2
                        radius: 4

                        Row {
                            anchors {
                                fill: parent
                                margins: 8
                            }
                            spacing: 12

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: "Theme:"
                                color: themePalette.text
                                font.pixelSize: 12
                                width: 80
                            }

                            ComboBox {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 150
                                model: settingsController ? settingsController.themeList : []
                                currentIndex: settingsController ? settingsController.currentThemeIndex : 0
                                onCurrentIndexChanged: {
                                    if (settingsController) {
                                        settingsController.currentThemeIndex = currentIndex;
                                    }
                                }
                            }
                        }
                    }
                }

                // Browsing section
                Column {
                    anchors {
                        fill: parent
                        margins: 16
                    }
                    spacing: 12
                    visible: settingsOverlay.currentSection === 1

                    Text {
                        text: settingsController && !settingsController.vaultUnlocked
                              ? "Unlock a vault to configure"
                              : "Browsing Defaults"
                        color: themePalette.text
                        font.pixelSize: 12
                        font.bold: true
                    }

                    // Browsing settings rows
                    Column {
                        visible: settingsController && settingsController.vaultUnlocked
                        width: parent.width
                        spacing: 4

                        // Row 0: Default sort order
                        Rectangle {
                            width: parent.width - 16
                            height: 40
                            color: (settingsOverlay.inPane && settingsOverlay.currentRow === 0)
                                   ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2)
                                   : "transparent"
                            border.color: (settingsOverlay.inPane && settingsOverlay.currentRow === 0)
                                         ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Row {
                                anchors { fill: parent; margins: 8 }
                                spacing: 12

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Default sort:"
                                    color: themePalette.text
                                    font.pixelSize: 11
                                    width: 100
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: (settingsController && settingsController.sortOrderList.length > settingsController.currentSortOrderIndex)
                                          ? settingsController.sortOrderList[settingsController.currentSortOrderIndex]
                                          : "—"
                                    color: themePalette.textDim
                                    font.pixelSize: 11
                                }
                            }
                        }

                        // Row 1: Show tags in tiles
                        Rectangle {
                            width: parent.width - 16
                            height: 40
                            color: (settingsOverlay.inPane && settingsOverlay.currentRow === 1)
                                   ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2)
                                   : "transparent"
                            border.color: (settingsOverlay.inPane && settingsOverlay.currentRow === 1)
                                         ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Row {
                                anchors { fill: parent; margins: 8 }
                                spacing: 12

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Show tags:"
                                    color: themePalette.text
                                    font.pixelSize: 11
                                    width: 100
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: settingsController && settingsController.tilesShowTags ? "Yes" : "No"
                                    color: themePalette.textDim
                                    font.pixelSize: 11
                                }
                            }
                        }

                        // Row 2: Auto-lock idle seconds (WS1 autolock controller, session-scoped)
                        Rectangle {
                            width: parent.width - 16
                            height: 40
                            color: (settingsOverlay.inPane && settingsOverlay.currentRow === 2)
                                   ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2)
                                   : "transparent"
                            border.color: (settingsOverlay.inPane && settingsOverlay.currentRow === 2)
                                         ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Row {
                                anchors { fill: parent; margins: 8 }
                                spacing: 12

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Auto-lock (seconds):"
                                    color: themePalette.text
                                    font.pixelSize: 11
                                    width: 140
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: autoLock ? autoLock.idleSeconds.toString() : "300"
                                    color: themePalette.textDim
                                    font.pixelSize: 11
                                }
                            }
                        }

                        // Row 3: Keep unlocked toggle (WS1 autolock controller, session-scoped)
                        Rectangle {
                            width: parent.width - 16
                            height: 40
                            color: (settingsOverlay.inPane && settingsOverlay.currentRow === 3)
                                   ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2)
                                   : "transparent"
                            border.color: (settingsOverlay.inPane && settingsOverlay.currentRow === 3)
                                         ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Row {
                                anchors { fill: parent; margins: 8 }
                                spacing: 12

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "Keep unlocked:"
                                    color: themePalette.text
                                    font.pixelSize: 11
                                    width: 140
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: autoLock && autoLock.keepUnlocked ? "Yes" : "No"
                                    color: themePalette.textDim
                                    font.pixelSize: 11
                                }
                            }
                        }

                    }
                }

                // Tag Colours section
                Column {
                    anchors {
                        fill: parent
                        margins: 16
                    }
                    spacing: 12
                    visible: settingsOverlay.currentSection === 2

                    Text {
                        text: settingsController && !settingsController.vaultUnlocked
                              ? "Unlock a vault to configure"
                              : "Tag Categories"
                        color: themePalette.text
                        font.pixelSize: 12
                        font.bold: true
                    }

                    // Category list with focus indication and inline rename
                    Column {
                        width: parent.width
                        spacing: 4
                        visible: settingsController && settingsController.vaultUnlocked

                        Repeater {
                            model: settingsController ? settingsController.categories : []

                            Rectangle {
                                width: parent.width - 16
                                height: 40
                                color: (settingsOverlay.inPane && settingsOverlay.currentRow === index)
                                       ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2)
                                       : "transparent"
                                border.color: (settingsOverlay.inPane && settingsOverlay.currentRow === index)
                                             ? themePalette.accent : "transparent"
                                border.width: 2
                                radius: 4

                                Row {
                                    anchors {
                                        fill: parent
                                        margins: 8
                                    }
                                    spacing: 8

                                    Rectangle {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 24
                                        height: 24
                                        radius: 4
                                        color: themePalette.text
                                        opacity: 0.3
                                    }

                                    // Show name or inline edit field
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.name
                                        color: themePalette.text
                                        font.pixelSize: 11
                                        visible: !settingsOverlay.renamingCategory || settingsOverlay.renamingRow !== index
                                    }

                                    TextInput {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: settingsOverlay.renamingText
                                        color: themePalette.text
                                        font.pixelSize: 11
                                        selectByMouse: true
                                        visible: settingsOverlay.renamingCategory && settingsOverlay.renamingRow === index
                                        focus: visible
                                        onTextChanged: settingsOverlay.renamingText = text

                                        Keys.onPressed: (event) => {
                                            if (event.key === Qt.Key_Return) {
                                                // Commit rename
                                                if (settingsController) {
                                                    settingsController.renameCategory(settingsOverlay.renamingRow, settingsOverlay.renamingText);
                                                    settingsOverlay.renamingCategory = false;
                                                    settingsOverlay.renamingRow = -1;
                                                    settingsOverlay.renamingText = "";
                                                }
                                                event.accepted = true;
                                                contentPanel.forceActiveFocus();
                                            } else if (event.key === Qt.Key_Escape) {
                                                // Cancel rename without closing overlay
                                                settingsOverlay.renamingCategory = false;
                                                settingsOverlay.renamingRow = -1;
                                                settingsOverlay.renamingText = "";
                                                event.accepted = true;
                                                contentPanel.forceActiveFocus();
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Add category hint (N key)
                        Text {
                            text: "(N) Add, (R) Rename, (Del) Remove"
                            color: themePalette.textDim
                            font.pixelSize: 10
                            topPadding: 8
                        }
                    }
                }
            }
        }

        // Error line in footer
        Rectangle {
            id: footerLine
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
            height: 30
            color: themePalette.bg
            radius: 8

            Text {
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: 12
                    rightMargin: 12
                }
                text: settingsController ? settingsController.errorLine : ""
                color: settingsController && settingsController.errorLine.length > 0
                       ? themePalette.danger : "transparent"
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }

        // Key handlers (bypass if renaming category)
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                if (settingsOverlay.renamingCategory) {
                    // Cancel rename, don't close overlay
                    settingsOverlay.renamingCategory = false;
                    settingsOverlay.renamingRow = -1;
                    settingsOverlay.renamingText = "";
                    event.accepted = true;
                } else {
                    settingsOverlay.close();
                    event.accepted = true;
                }
            }
            else if (event.key === Qt.Key_F2 && !settingsOverlay.renamingCategory) {
                // F2 closes too: the overlay holds focus while open, so the
                // shell's F2 toggle can't see the key (T3.1 W5)
                settingsOverlay.close();
                event.accepted = true;
            }
            else if (!settingsOverlay.renamingCategory) {
                // Only process navigation/action keys if not renaming
                if (event.key === Qt.Key_Tab) {
                    settingsOverlay.inPane = !settingsOverlay.inPane;
                    if (settingsOverlay.inPane) {
                        settingsOverlay.currentRow = 0;  // Reset row when entering pane
                    }
                    event.accepted = true;
                }
                else if (event.key === Qt.Key_Up) {
                    settingsOverlay.moveRow(-1);
                    event.accepted = true;
                }
                else if (event.key === Qt.Key_Down) {
                    settingsOverlay.moveRow(1);
                    event.accepted = true;
                }
                else if (event.key === Qt.Key_Left) {
                    settingsOverlay.changeValue(-1);
                    event.accepted = true;
                }
                else if (event.key === Qt.Key_Right) {
                    settingsOverlay.changeValue(1);
                    event.accepted = true;
                }
                else if (event.key === Qt.Key_N && settingsOverlay.currentSection === 2 && settingsController && settingsController.vaultUnlocked && settingsOverlay.inPane) {
                    // Add category in Tag Colours section
                    settingsController.addCategory("New Category");
                    event.accepted = true;
                }
                else if (event.key === Qt.Key_R && settingsOverlay.currentSection === 2 && settingsController && settingsController.vaultUnlocked && settingsOverlay.inPane) {
                    // Start inline rename
                    if (settingsOverlay.currentRow < (settingsController.categories ? settingsController.categories.length : 0)) {
                        settingsOverlay.renamingCategory = true;
                        settingsOverlay.renamingRow = settingsOverlay.currentRow;
                        settingsOverlay.renamingText = settingsController.categories[settingsOverlay.currentRow].name;
                    }
                    event.accepted = true;
                }
                else if (event.key === Qt.Key_Delete && settingsOverlay.currentSection === 2 && settingsController && settingsController.vaultUnlocked && settingsOverlay.inPane) {
                    // Remove category
                    if (settingsOverlay.currentRow < (settingsController.categories ? settingsController.categories.length : 0)) {
                        settingsController.removeCategory(settingsOverlay.currentRow);
                    }
                    event.accepted = true;
                }
            }
        }
    }
}
