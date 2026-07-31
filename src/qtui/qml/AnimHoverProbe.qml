import QtQuick

// Hover auto-play gate for animated tiles (Finding 2 - WS3 review).
// Self-contained component: tracks ≥200ms dwell, checks 1920×1080/300-frame budgets.
// Contract: when dwell ≥200ms + budgets OK, calls onHoverStart; when leaving, calls onHoverStop.
//
// Phase 57 integration (pending):
// - Bind isAnimated from model role
// - Bind frameCount from model role
// - Connect onHoverStart/Stop to animate SecureImageItem via AnimController

Rectangle {
    id: probe
    color: "transparent"  // Invisible probe layer

    // Input: tile properties (bindings to be added in Phase 57)
    property bool isAnimated: false         // Bind to model.isAnimated once available
    property int imageWidth: 0              // Image dimensions
    property int imageHeight: 0
    property int frameCount: 0              // Total frame count (0 = unknown/static)
    property var onHoverStart: null         // Callback when auto-play should start
    property var onHoverStop: null          // Callback when auto-play should stop

    // Constants (from ui/anim_model.h via tdd_anim_hover_probe_test)
    readonly property double kAnimHoverDwell: 0.200
    readonly property int kAnimHoverMaxWidth: 1920
    readonly property int kAnimHoverMaxHeight: 1080
    readonly property int kAnimHoverMaxFrames: 300

    // Internal state
    property double hoverDwell: 0.0         // Accumulated hover time
    property bool withinBudget: false       // Cached budget check result
    property bool animStarted: false        // Guard to prevent double-start

    // HoverHandler: detect cursor entry/leave
    HoverHandler {
        onHoveredChanged: {
            if (hovered) {
                // Cursor entered: reset dwell and start timer
                probe.hoverDwell = 0.0
                probe.animStarted = false
                hoverTimer.start()
            } else {
                // Cursor left: stop timer and cleanup
                hoverTimer.stop()
                probe.hoverDwell = 0.0
                probe.animStarted = false
                if (probe.onHoverStop) probe.onHoverStop()
            }
        }
    }

    // Timer: accumulate dwell time (60 FPS polling)
    Timer {
        id: hoverTimer
        interval: 16  // ~60 FPS
        repeat: true
        onTriggered: {
            if (!probe.isAnimated) return

            probe.hoverDwell += 0.016  // 16ms per tick

            // Check: crossed dwell threshold + budgets OK + not already started?
            if (probe.hoverDwell >= probe.kAnimHoverDwell && !probe.animStarted && probe.withinBudget) {
                probe.animStarted = true
                if (probe.onHoverStart) probe.onHoverStart(null)  // Controller would be wired here
                hoverTimer.stop()  // Gate locked until cursor leaves (anti-retrigger)
            }
        }
    }

    // Budget compliance check (called once on init)
    Component.onCompleted: {
        probe.withinBudget = (probe.isAnimated &&
                              probe.imageWidth <= probe.kAnimHoverMaxWidth &&
                              probe.imageHeight <= probe.kAnimHoverMaxHeight &&
                              probe.frameCount <= probe.kAnimHoverMaxFrames)
    }

    // Cleanup on destroy
    Component.onDestruction: {
        hoverTimer.stop()
    }
}
