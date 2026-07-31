import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: popup
    padding: 0
    margins: 0
    width: 200
    height: Math.min(contentHeight + 8, 300)

    // themePalette comes from the engine context property
    // (a shadowing property declaration breaks self-named bindings — T3.1 W5).
    // Public properties
    property var vocabulary: []  // Full list of available tags
    property string currentText: ""  // Current text in the input field
    property var textInput  // Reference to the TextInput to inject tags

    // Result signal
    signal tagSelected(string tag)

    // Computed filtered list based on current input
    property var filteredTags: {
        const words = currentText.toLowerCase().split(/[\s,]+/);
        if (words.length === 0 || !words[words.length - 1]) {
            return [];
        }
        const prefix = words[words.length - 1];
        return vocabulary.filter(tag => tag.toLowerCase().startsWith(prefix)).slice(0, 10);
    }

    property int selectedIndex: -1

    contentItem: Rectangle {
        color: themePalette.surface
        border.color: themePalette.border
        border.width: 1

        ListView {
            id: tagList
            anchors.fill: parent
            anchors.margins: 4
            model: popup.filteredTags
            spacing: 2
            clip: true

            delegate: Rectangle {
                width: tagList.width
                height: 24
                color: popup.selectedIndex === index ? themePalette.accent : "transparent"

                Text {
                    anchors {
                        left: parent.left
                        top: parent.top
                        bottom: parent.bottom
                        leftMargin: 4
                    }
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    color: popup.selectedIndex === index ? themePalette.bg : themePalette.text
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        popup.selectedIndex = index;
                        popup.insertTag(modelData);
                    }
                    onEntered: {
                        popup.selectedIndex = index;
                    }
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }

        Text {
            anchors.centerIn: parent
            text: popup.filteredTags.length === 0 ? "No matches" : ""
            color: themePalette.textDim
            font.pixelSize: 10
            visible: popup.filteredTags.length === 0
        }
    }

    onOpened: {
        selectedIndex = -1;
    }

    function insertTag(tag) {
        if (!textInput) return;

        const text = textInput.text;
        const words = text.split(/[\s,]+/);

        if (words.length > 0 && words[words.length - 1]) {
            // Replace the last word with the selected tag
            words[words.length - 1] = tag;
            textInput.text = words.join(" ");
        } else {
            // Append tag
            textInput.text = text + (text.length > 0 && text[text.length - 1] !== ' ' ? " " : "") + tag + " ";
        }

        popup.tagSelected(tag);
        popup.close();
    }

    function handleKeyPress(event) {
        if (event.key === Qt.Key_Escape) {
            popup.close();
            event.accepted = true;
            return true;
        } else if (event.key === Qt.Key_Up) {
            if (selectedIndex > 0) {
                selectedIndex--;
            } else if (selectedIndex < 0) {
                selectedIndex = filteredTags.length - 1;
            }
            event.accepted = true;
            return true;
        } else if (event.key === Qt.Key_Down) {
            if (selectedIndex < filteredTags.length - 1) {
                selectedIndex++;
            } else {
                selectedIndex = -1;
            }
            event.accepted = true;
            return true;
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab) {
            if (selectedIndex >= 0 && selectedIndex < filteredTags.length) {
                insertTag(filteredTags[selectedIndex]);
            }
            event.accepted = true;
            return true;
        }
        return false;
    }
}
