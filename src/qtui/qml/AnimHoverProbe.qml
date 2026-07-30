import QtQuick
import Osv 1.0

// Auto-play animated tiles on hover (≥200ms dwell, within 1920×1080/300-frame budget).
// Self-contained: tracks dwell, checks budgets, manages AnimController, prevents retrigger.
// Minimal integration: instantiate once per tile, pass isAnimated + dimensions.
//
// Animation starts when:
// - Cursor hovers ≥200ms continuously
// - Image dimensions ≤1920×1080 AND frame count ≤300
//
// Animation stops when:
// - Cursor leaves tile
// - Animation completes loop
// - New hover within gate window (anti-retrigger)

Item {
    id: probe

    // Input: tile properties
    required property bool isAnimated        // Is this tile an animated image?
    required property int imageWidth         // Image dimensions
    required property int imageHeight
    required property int frameCount         // Total frame count (0 if unknown)
    required property var onHoverStart       // Callback: (animController) when auto-play starts
    required property var onHoverStop        // Callback: () when auto-play stops

    // Constants (from ui/anim_model.h)
    readonly property double kAnimHoverDwell = 0.200   // seconds
    readonly property int kAnimHoverMaxWidth = 1920
    readonly property int kAnimHoverMaxHeight = 1080
    readonly property int kAnimHoverMaxFrames = 300

    // State
    property double hoverDwell: 0.0         // Time cursor has dwelled on this tile
    property bool withinBudget: false       // True if dimensions/frames within limits
    property var animController: null       // AnimController instance, or null

    // HoverHandler: track cursor presence
    HoverHandler {
        id: hoverHandler
        onHovered: {
            if (hovered) {
                // Cursor entered: start dwell timer
                hoverDwell = 0.0
                hoverTimer.start()
            } else {
                // Cursor left: stop and cleanup
                hoverTimer.stop()
                hoverDwell = 0.0
                if (animController !== null) {
                    animController.destroy()
                    animController = null
                    onHoverStop()
                }
            }
        }
    }

    // Timer: advance dwell time every ~16ms (60 FPS)
    Timer {
        id: hoverTimer
        interval: 16
        repeat: true
        onTriggered: {
            if (!probe.isAnimated) {
                return  // No animation to play
            }

            hoverDwell += 0.016

            // Check if we've crossed the dwell threshold
            if (hoverDwell >= kAnimHoverDwell && animController === null) {
                // Start animation if budgets allow
                if (withinBudget) {
                    animController = animControllerComponent.createObject(probe)
                    onHoverStart(animController)
                }
                // If not within budget, stop checking (gate is locked until hover restarts)
                hoverTimer.stop()
            }
        }
    }

    // Component template: create AnimController on demand
    Component {
        id: animControllerComponent
        AnimController {
            id: controller
        }
    }

    // Calculate budget compliance once per tile instantiation
    Component.onCompleted: {
        withinBudget = checkBudgets()
    }

    // Helper: check if this tile is within hover animation budgets
    function checkBudgets() {
        if (!isAnimated) {
            return false
        }

        // Dimension budget: ≤1920×1080
        if (imageWidth > kAnimHoverMaxWidth || imageHeight > kAnimHoverMaxHeight) {
            return false
        }

        // Frame budget: ≤300 frames
        if (frameCount > kAnimHoverMaxFrames) {
            return false
        }

        return true
    }

    // Cleanup on destruction
    Component.onDestruction: {
        hoverTimer.stop()
        if (animController !== null) {
            animController.destroy()
        }
    }
}
