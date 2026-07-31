import QtQuick
import Osv 1.0

// A single tag chip: colored dot + display text
// Properties:
//   - tag: raw tag string (e.g., "artist:Kaguya")
//   - displayText: display text after category prefix strip (e.g., "Kaguya")
//   - swatchIndex: color swatch index (-1 for uncategorized/textDim)
Item {
    id: chip
    width: dotRadius * 2 + dotGap + textMetrics.width
    height: chipRowH  // ui::CHIP_ROW_H from tag_chip.h

    // Public properties
    property string tag: ""
    property string displayText: tag
    property int swatchIndex: -1

    // Constants (match ui::tag_chip.h)
    property real dotRadius: 4.5
    property real dotGap: 7.0
    property real chipRowH: 16.0  // ui::CHIP_ROW_H

    TextMetrics {
        id: textMetrics
        font: chipText.font
        text: displayText
    }

    // Colored dot
    Rectangle {
        id: dot
        x: 0
        y: (parent.height - height) / 2
        width: dotRadius * 2
        height: dotRadius * 2
        radius: dotRadius
        color: {
            if (swatchIndex < 0) {
                return themePalette.textDim
            }
            // Look up the actual color from the theme palette based on swatch index
            // For now, use accent for positive indices; real implementation queries theme
            return themePalette.accent
        }
    }

    // Display text
    Text {
        id: chipText
        anchors {
            left: dot.right
            leftMargin: dotGap
            verticalCenter: parent.verticalCenter
        }
        text: displayText
        color: swatchIndex < 0 ? themePalette.textDim : themePalette.text
        font.pixelSize: 12
        elide: Text.ElideRight
    }
}
