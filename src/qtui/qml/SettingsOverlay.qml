import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: settingsOverlay
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0)  // Transparent initially
    visible: false
    z: 1000  // Above other content

    // Public API
    property var settingsController: null

    // Internal state
    property bool isOpen: false
    property bool inPane: false  // false: rail focus, true: pane focus
    property int currentSection: 0  // 0=Appearance, 1=Browsing, 2=TagColours
    property int currentRow: 0  // focused row within section

    // Signal emitted when overlay closes to allow focus restoration
    signal closed()

    // Calculate row count for current section (matches settings_model logic)
    function rowCountForSection(section) {
        if (section === 0) return 1;  // Appearance: theme
        if (section === 1) {
            // Browsing: 4 rows if unlocked, 0 if locked
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
            settingsController.setCurrentThemeIndex(idx);
        }
        // Browsing section: only if unlocked
        else if (currentSection === 1 && settingsController.vaultUnlocked) {
            // Placeholder: would handle sort order, toggles, etc. via SettingsController
        }
        // TagColours section: only if unlocked
        else if (currentSection === 2 && settingsController.vaultUnlocked) {
            // Placeholder: would handle category swatch cycling via SettingsController
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
                                        settingsController.setCurrentThemeIndex(currentIndex);
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

                    // Placeholder rows for browsing settings
                    Column {
                        visible: settingsController && settingsController.vaultUnlocked
                        width: parent.width
                        spacing: 4

                        Rectangle {
                            width: parent.width - 16
                            height: 40
                            visible: settingsOverlay.currentRow === 0
                            color: settingsOverlay.inPane ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2) : "transparent"
                            border.color: settingsOverlay.inPane ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Text {
                                anchors { fill: parent; margins: 8 }
                                text: "Default sort order"
                                color: themePalette.text
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Rectangle {
                            width: parent.width - 16
                            height: 40
                            visible: settingsOverlay.currentRow === 1
                            color: settingsOverlay.inPane ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2) : "transparent"
                            border.color: settingsOverlay.inPane ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Text {
                                anchors { fill: parent; margins: 8 }
                                text: "Show tags in tiles"
                                color: themePalette.text
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Rectangle {
                            width: parent.width - 16
                            height: 40
                            visible: settingsOverlay.currentRow === 2
                            color: settingsOverlay.inPane ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2) : "transparent"
                            border.color: settingsOverlay.inPane ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Text {
                                anchors { fill: parent; margins: 8 }
                                text: "Auto-lock after (seconds)"
                                color: themePalette.text
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Rectangle {
                            width: parent.width - 16
                            height: 40
                            visible: settingsOverlay.currentRow === 3
                            color: settingsOverlay.inPane ? Qt.rgba(themePalette.accent.r, themePalette.accent.g, themePalette.accent.b, 0.2) : "transparent"
                            border.color: settingsOverlay.inPane ? themePalette.accent : "transparent"
                            border.width: 2
                            radius: 4

                            Text {
                                anchors { fill: parent; margins: 8 }
                                text: "Keep vault unlocked"
                                color: themePalette.text
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
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

                    // Category list with focus indication
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

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: modelData.name
                                        color: themePalette.text
                                        font.pixelSize: 11
                                    }
                                }
                            }
                        }

                        // Add category hint (N key)
                        Text {
                            text: "(N) Add category"
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

        // Key handlers
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                settingsOverlay.close();
                event.accepted = true;
            }
            else if (event.key === Qt.Key_Tab) {
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
                // Rename category - placeholder
                if (settingsOverlay.currentRow < (settingsController.categories ? settingsController.categories.length : 0)) {
                    // Would open inline rename dialog
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
