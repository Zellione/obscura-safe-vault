import QtQuick
import Osv 1.0

// A row of tag chips with optional "+N" overflow indicator
// Properties:
//   - tags: list of raw tag strings
//   - maxWidth: maximum width to constrain chips
//   - maxLines: maximum number of lines (usually 1 for grid tiles)
Item {
    id: chipRow
    height: 30  // ui::CHIP_LINE_H = 30

    // Public properties
    required property var tags
    property real maxWidth: 200
    property int maxLines: 1

    // Layout the chips: TODO in Phase 49 fully implement pack_chip_lines
    // For now, simplified: show all chips on one line with "+N" if overflow
    Row {
        id: chipContainer
        spacing: 18  // ui::CHIP_SPACING
        anchors {
            left: parent.left
            verticalCenter: parent.verticalCenter
        }
        clip: true
        width: Math.min(parent.maxWidth, implicitWidth)

        Repeater {
            model: chipRow.tags.length
            delegate: TagChip {
                tag: chipRow.tags[index]
                displayText: tagController.getTagDisplayText(chipRow.tags[index])
                swatchIndex: tagController.getTagSwatchIndex(chipRow.tags[index])
            }
        }
    }

    // TODO: +N overflow indicator for chips that don't fit
}
