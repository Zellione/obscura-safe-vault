import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: "Volume Set"

    required property var themePalette
    required property var volumeSet
    required property bool allVolumesPresent
    property var missingOrdinals: []  // ordinals (1-indexed) of missing volumes

    // When volumes are missing, Enter/Return must NOT accept the dialog
    standardButtons: allVolumesPresent ? (Dialog.Ok | Dialog.Cancel) : Dialog.Cancel

    ColumnLayout {
        width: 400
        spacing: 12
        anchors.fill: parent
        anchors.margins: 8

        // Main panel with DANGER border when missing
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.themePalette.surface
            border.color: root.allVolumesPresent ? root.themePalette.border : root.themePalette.danger
            border.width: root.allVolumesPresent ? 1 : 2
            radius: 4

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                // Warning banner (only when missing)
                Rectangle {
                    Layout.fillWidth: true
                    height: 40
                    color: root.themePalette.danger
                    radius: 2
                    visible: !root.allVolumesPresent

                    Text {
                        anchors.centerIn: parent
                        text: "Missing volumes — cannot import"
                        color: root.themePalette.bg
                        font.bold: true
                    }
                }

                // Archive stem
                Text {
                    text: "Archive: " + root.volumeSet.stem
                    color: root.themePalette.text
                    font.bold: true
                }

                // Volumes list
                Text {
                    text: "Volumes:"
                    color: root.themePalette.textDim
                    font.pixelSize: 11
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    ListView {
                        model: root.volumeSet.volumes
                        spacing: 2

                        delegate: Text {
                            text: {
                                var ordinal = index + 1;
                                var missing = root.missingOrdinals.includes(ordinal);
                                var marker = missing ? " (missing)" : " (present)";
                                return ordinal + ". " + modelData + marker;
                            }
                            color: {
                                var ordinal = index + 1;
                                return root.missingOrdinals.includes(ordinal) ?
                                       root.themePalette.danger :
                                       root.themePalette.text;
                            }
                        }
                    }
                }

                // "… and N more" elision indicator
                Text {
                    Layout.fillWidth: true
                    text: {
                        var count = root.volumeSet.volumes.length;
                        return "… and " + (count - 8) + " more";
                    }
                    visible: root.volumeSet.volumes.length > 8
                    color: root.themePalette.textFaint
                    font.pixelSize: 10
                }
            }
        }

        // Keyboard hint (shows "Esc — cancel" when missing, normal buttons when complete)
        Text {
            Layout.fillWidth: true
            text: root.allVolumesPresent ? "" : "Press Esc to cancel"
            color: root.themePalette.textDim
            font.pixelSize: 10
            visible: !root.allVolumesPresent
        }
    }

    onAccepted: {
        if (root.allVolumesPresent) {
            // Proceed with import
            root.close();
        }
        // If volumes missing, Enter should have no effect (button grayed out)
    }

    onRejected: {
        root.close();
    }

    Keys.onEscapePressed: {
        root.rejected();
    }

    focus: visible
}
