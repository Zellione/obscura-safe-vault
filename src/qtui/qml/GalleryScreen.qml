import QtQuick
import QtQuick.Controls
import Osv 1.0

Rectangle {
    id: galleryRoot
    color: themePalette.bg
    // No anchors here: StackView manages the current item's geometry
    // (anchors on a StackView child trigger "conflicting anchors" warnings).

    // Dialog for renaming items (passed from Main.qml)
    property var renameDialog: null

    // T3.1 W5: quick-search overlay + tag editor dialog (passed from Main.qml)
    property var searchOverlay: null
    property var tagEditorDialog: null

    // Help groups for F1 help popup
    property var helpGroups: [
        {
            title: "Gallery Navigation",
            entries: [
                { keys: "Arrow Keys", description: "Navigate gallery" },
                { keys: "Enter", description: "Open image/video or enter gallery" },
                { keys: "Esc", description: "Go up one level" },
                { keys: "Shift+S", description: "Cycle sort order" },
                { keys: "L", description: "Cycle view density (grid/list)" },
                { keys: "Ctrl+D", description: "Toggle detail panel (WS2 Task 2.3)" }
            ]
        },
        {
            title: "Multi-Selection (WS2 Task 2.2)",
            entries: [
                { keys: "Space", description: "Toggle selection of current item" },
                { keys: "Click", description: "Toggle selection" },
                { keys: "Shift+Click", description: "Range select" },
                { keys: "Ctrl+A", description: "Select all items" }
            ]
        },
        {
            title: "Detail Panel (WS2 Task 2.3)",
            entries: [
                { keys: "Ctrl+Up/Down", description: "Scroll detail panel" },
                { keys: "Mouse Wheel", description: "Scroll over panel" }
            ]
        },
        {
            title: "Vault Operations (WS4)",
            entries: [
                { keys: "E", description: "Export selected items" },
                { keys: "Del", description: "Delete selected items" },
                { keys: "M", description: "Move selected items" },
                { keys: "Shift+M", description: "Copy selected items" },
                { keys: "Shift+C", description: "Compact vault" },
                { keys: "O", description: "Import from folder" },
                { keys: "Ctrl+O", description: "Import from files" },
                { keys: "Z", description: "Import from archive" },
                { keys: "Shift+I", description: "Import status screen" }
            ]
        },
        {
            title: "Tags, Search & Favorites (WS5)",
            entries: [
                { keys: "/", description: "Quick search" },
                { keys: "Shift+/", description: "Advanced search" },
                { keys: "G", description: "Edit tags of focused item" },
                { keys: "Shift+G", description: "Import tag list to gallery" },
                { keys: "B", description: "Toggle favorite" },
                { keys: "Shift+F", description: "Favorites screen" },
                { keys: "Shift+T", description: "Tag overview" },
                { keys: "R", description: "Rename focused item" }
            ]
        }
    ]

    // Signal for back navigation (Main.qml wires to galleryModel.upOneLevel)
    signal back()

    // View mode helpers (synchronized with sessionState)
    // GalleryView enum: 0=List, 1=GridS, 2=GridM, 3=GridL, 4=GridXL
    readonly property var cellSizes: [0, 128, 188, 248, 320]  // index 0 (List) is ignored
    readonly property int currentViewMode: sessionState.viewDensity

    // Temporary storage for selected node keys (WS4 shortcuts)
    property var currentNodeKeys: []

    function nextViewMode() {
        // Cycle: 0(List) -> 1(GridS) -> 2(GridM) -> 3(GridL) -> 4(GridXL) -> 0(List)
        sessionState.viewDensity = (currentViewMode + 1) % 5
    }

    // T3.1 W5: UI-style path of the row's node ("/name" at root, "/gal/name" nested) —
    // the path form tagController / favoritesController expect.
    function nodePathAt(row) {
        const name = galleryModel.nameAt(row)
        if (name === "") return ""
        return galleryModel.currentPath === "/" ? "/" + name
                                                : galleryModel.currentPath + "/" + name
    }

    function getSelectedNodeKeys() {
        // Collect node keys from selected items, or current item if nothing selected
        currentNodeKeys = []
        if (selectionController.count > 0) {
            // Get node keys for all selected items by iterating through grid
            for (let i = 0; i < grid.model.count; i++) {
                const nodeName = grid.model.data(grid.model.index(i, 0), GalleryModel.NameRole)
                if (selectionController.isSelected(i)) {
                    const nodeKey = grid.model.data(grid.model.index(i, 0), GalleryModel.NodeKeyRole)
                    currentNodeKeys.push(nodeKey)
                }
            }
        } else if (grid.currentIndex >= 0 && grid.currentIndex < grid.model.count) {
            // Use current item if no selection
            const nodeKey = grid.model.data(grid.model.index(grid.currentIndex, 0), GalleryModel.NodeKeyRole)
            currentNodeKeys.push(nodeKey)
        }
    }

    // `focus: true` only grants scope focus inside the StackView;
    // keys (Esc = up, arrows) need ACTIVE focus on the grid — force
    // it when the page appears and again when the viewer/video
    // screen pops back to it.
    Component.onCompleted: grid.forceActiveFocus()
    StackView.onActivated: grid.forceActiveFocus()
    // T3.1 W5: popups restore focus to the screen ROOT (stack.currentItem);
    // delegate it down to the grid where the key handlers live.
    onActiveFocusChanged: if (activeFocus) grid.forceActiveFocus()

    // Header: current gallery path + mouse affordance to go up.
    Rectangle {
        id: galleryHeader
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 44
        color: themePalette.surface

        Row {
            anchors {
                verticalCenter: parent.verticalCenter
                left: parent.left
                leftMargin: 8
            }
            spacing: 10

            Rectangle {
                width: 72
                height: 30
                radius: 4
                visible: galleryModel.currentPath !== "/"
                color: upMouse.pressed ? themePalette.surfaceHi : themePalette.bg
                border.color: themePalette.border
                border.width: 1
                anchors.verticalCenter: parent.verticalCenter

                MouseArea {
                    id: upMouse
                    anchors.fill: parent
                    onClicked: {
                        // T3.1 W5: just go up — emitting back() after upOneLevel
                        // made the shell see currentPath === "/" and LOCK the
                        // vault when leaving a first-level gallery.
                        if (galleryModel.currentPath !== "/") {
                            galleryModel.upOneLevel()
                            grid.forceActiveFocus()
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: "⬆ Up"
                    color: themePalette.text
                    font.pixelSize: 13
                }
            }

            // WS2 Task 2.5: Breadcrumb with "Home" root + clickable segments
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                Text {
                    text: "Home"
                    color: themePalette.textDim
                    font.pixelSize: 13
                    font.underline: homeMouse.containsMouse
                    MouseArea {
                        id: homeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            if (galleryModel.currentPath !== "/") {
                                // Go to root by repeatedly calling upOneLevel
                                while (galleryModel.currentPath !== "/") {
                                    galleryModel.upOneLevel()
                                }
                            }
                        }
                    }
                }

                Text {
                    visible: galleryModel.currentPath !== "/"
                    text: " / "
                    color: themePalette.textDim
                    font.pixelSize: 13
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        let path = galleryModel.currentPath
                        if (path === "/") return ""
                        let label = galleryModel.sortLabel()
                        if (label) {
                            return path.substring(1) + "   Sort: " + label
                        }
                        return path.substring(1)
                    }
                    color: themePalette.textDim
                    font.pixelSize: 13
                    elide: Text.ElideLeft
                }
            }
        }
    }

    // Gallery grid view: displays galleries and media from galleryModel.
    // Galleries shown as folder glyphs, media as thumbnails.
    // Arrow keys navigate, Enter opens, Esc up/back, Shift+S sort, L view mode.
    // WS2 Task 2.3: Grid reflows left when detail panel is open (no overlay).
    GridView {
        id: grid
        anchors {
            top: galleryHeader.bottom
            left: parent.left
            right: detailPanel.left
            bottom: galleryFooter.top
        }
        cellWidth: currentViewMode === 0 ? width : cellSizes[currentViewMode] + 12
        // List row height (mode 0): 44px per SDL ROW_H (two-line name + metadata layout)
        // Grid modes: cell size + 12px padding
        cellHeight: currentViewMode === 0 ? 44 : cellSizes[currentViewMode] + 12
        model: galleryModel
        focus: true

        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                anchors.fill: parent
                anchors.margins: 6
                color: GridView.isCurrentItem ? themePalette.surfaceHi : themePalette.surface
                radius: 6
                // WS2 Task 2.2: Show ACCENT border when selected
                border.color: selectionController.isSelected(index) ? themePalette.accent : "transparent"
                border.width: selectionController.isSelected(index) ? 2 : 0

                // Gallery cover or media thumbnail (Finding 1, 2)
                Item {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                        bottom: countsLabel.visible ? countsLabel.top : parent.bottom
                    }
                    anchors.margins: 6
                    anchors.bottomMargin: model.isGallery ? 6 : 26  // Reserve space for counts if gallery

                    // Media thumbnail
                    SecureImageItem {
                        visible: !model.isGallery
                        anchors.fill: parent
                        nodeKey: model.nodeKey
                    }

                    // Gallery cover image (if available) or folder icon
                    SecureImageItem {
                        visible: model.isGallery && model.cover !== undefined && model.cover !== null
                        anchors.fill: parent
                        // cover node's key (T3.1 W5: was a chunk offset — crashed ThumbCache);
                        // media rows have no cover → bind 0, the item stays invisible
                        nodeKey: (model.cover !== undefined && model.cover !== null) ? model.cover : 0
                    }

                    // Folder icon fallback for galleries without covers (Finding 1)
                    Text {
                        visible: model.isGallery && (model.cover === undefined || model.cover === null)
                        anchors.centerIn: parent
                        text: "📁"
                        font.pixelSize: 48
                    }
                }

                // Animated badge "A" (Finding 4)
                Rectangle {
                    visible: model.isAnimated && !model.isGallery
                    anchors {
                        top: parent.top
                        right: parent.right
                        margins: 6
                    }
                    width: 20
                    height: 20
                    radius: 4
                    color: themePalette.accent
                    Text {
                        anchors.centerIn: parent
                        text: "A"
                        color: themePalette.bg
                        font.bold: true
                        font.pixelSize: 11
                    }
                }

                // Favorite star (Finding 4)
                Text {
                    visible: model.isFavorite
                    anchors {
                        top: parent.top
                        right: parent.right
                        margins: 4
                    }
                    text: "★"
                    color: themePalette.favorite
                    font.pixelSize: 20
                }

                // Child counts for galleries (Finding 2)
                Text {
                    id: countsLabel
                    visible: model.isGallery && model.childCounts !== undefined
                    anchors {
                        bottom: parent.bottom
                        left: parent.left
                        right: parent.right
                        margins: 4
                    }
                    text: model.childCounts || ""
                    color: themePalette.textDim
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }

                // Name label — anchors above counts when visible (BREAKAGE FIX)
                Text {
                    anchors {
                        bottom: countsLabel.visible ? countsLabel.top : parent.bottom
                        horizontalCenter: parent.horizontalCenter
                        margins: 4
                    }
                    width: parent.width - 12
                    elide: Text.ElideMiddle
                    text: model.name
                    color: themePalette.text
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 12
                }

                // WS3 Finding 2: Hover auto-play gate (AnimHoverProbe QML integration)
                AnimHoverProbe {
                    anchors.fill: parent
                    visible: !model.isGallery && model.isAnimated
                    isAnimated: model.isAnimated
                    // frameCount defaults to 0 (unknown) since vault doesn't store frame metadata
                    // This is safe: 0 <= kAnimHoverMaxFrames (300), so budget check passes
                    onHoverStart: (ctrl) => {
                        // Animation control wired here in Phase 57 once SecureImageItem
                        // exposes animation playback API
                    }
                    onHoverStop: {
                        // Cleanup wired here in Phase 57
                    }
                }
            }

            // Single-tap: select item (and give the grid key focus,
            // so Esc/arrows work after any mouse interaction)
            TapHandler {
                onTapped: {
                    grid.currentIndex = index
                    grid.forceActiveFocus()
                    // WS2 Task 2.2: Toggle selection on click
                    selectionController.toggle(index)
                }
            }

            // Shift+Click: range select (WS2 Task 2.2)
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: (eventPoint) => {
                    if (eventPoint.modifiers & Qt.ShiftModifier) {
                        selectionController.rangeSelectTo(index)
                    }
                }
            }

            // Double-tap: activate (open gallery or image viewer)
            TapHandler {
                acceptedButtons: Qt.LeftButton
                gesturePolicy: TapHandler.DragThreshold
                onDoubleTapped: galleryModel.activate(index)
            }
        }

        // Keyboard navigation
        Keys.onReturnPressed: {
            galleryModel.activate(grid.currentIndex)
        }
        Keys.onEscapePressed: {
            // T3.1 W5: Esc inside a gallery goes UP one level; only Esc at the
            // root emits back() (shell locks + returns to the vault manager).
            // The old up-then-back sequence locked the vault from any
            // first-level gallery.
            if (galleryModel.currentPath === "/") {
                galleryRoot.back()
            } else {
                galleryModel.upOneLevel()
            }
        }
        Keys.onSpacePressed: {
            // WS2 Task 2.2: Space toggles selection
            selectionController.toggle(grid.currentIndex)
        }
        // Note: F2 is now global (opens settings overlay from Main.qml)
        // T3.1 W5: all shortcuts match on event.key (layout-stable). The previous
        // event.text === "E"-style matching never fired for unshifted keys ("e" != "E")
        // and never fired for Ctrl chords (Ctrl+O produces a control character).
        Keys.onPressed: (event) => {
            const shift = event.modifiers & Qt.ShiftModifier
            const ctrl = event.modifiers & Qt.ControlModifier
            if (event.key === Qt.Key_S && shift && !ctrl) {
                // Shift+S: cycle sort order
                galleryModel.nextSort()
                event.accepted = true
            } else if (event.key === Qt.Key_A && ctrl) {
                // WS2 Task 2.2: Ctrl+A select all
                selectionController.toggleAll(grid.model.count)
                event.accepted = true
            } else if (event.key === Qt.Key_L && !shift && !ctrl) {
                // L: cycle view density
                nextViewMode()
                event.accepted = true
            } else if (event.key === Qt.Key_D && ctrl) {
                // WS2 Task 2.3: Ctrl+D toggle detail panel
                sessionState.detailOpen = !sessionState.detailOpen
                event.accepted = true
            } else if (event.key === Qt.Key_E && !shift && !ctrl) {
                // WS4: E - export selected items
                getSelectedNodeKeys()
                if (currentNodeKeys.length > 0) {
                    exportController.startExport("", currentNodeKeys)
                }
                event.accepted = true
            } else if (event.key === Qt.Key_Delete) {
                // WS4: Del - delete selected items
                getSelectedNodeKeys()
                if (currentNodeKeys.length > 0) {
                    transferController.deleteItems(currentNodeKeys)
                }
                event.accepted = true
            } else if (event.key === Qt.Key_M && !shift && !ctrl) {
                // WS4: M - move selected items
                getSelectedNodeKeys()
                if (currentNodeKeys.length > 0) {
                    transferController.transferItems(currentNodeKeys, false, "", "")
                }
                event.accepted = true
            } else if (event.key === Qt.Key_M && shift && !ctrl) {
                // WS4: Shift+M - copy selected items
                getSelectedNodeKeys()
                if (currentNodeKeys.length > 0) {
                    transferController.transferItems(currentNodeKeys, true, "", "")
                }
                event.accepted = true
            } else if (event.key === Qt.Key_C && shift && !ctrl) {
                // WS4: Shift+C - compact vault
                transferController.compact()
                event.accepted = true
            } else if (event.key === Qt.Key_O && !ctrl && !shift) {
                // WS4: O - import from folders
                importController.pickFolders()
                event.accepted = true
            } else if (event.key === Qt.Key_O && ctrl) {
                // WS4: Ctrl+O - import from files
                importController.pickFiles()
                event.accepted = true
            } else if (event.key === Qt.Key_Z && !ctrl && !shift) {
                // WS4: Z - import from archive
                importController.pickArchives()
                event.accepted = true
            } else if (event.key === Qt.Key_Slash && !ctrl && !shift) {
                // WS5: / - quick search overlay (Shift+/ = advanced search, handled globally)
                if (searchOverlay) searchOverlay.open()
                event.accepted = true
            } else if (event.key === Qt.Key_G && !ctrl && !shift) {
                // WS5: G - edit tags of focused item
                if (tagEditorDialog && grid.currentIndex >= 0) {
                    const path = nodePathAt(grid.currentIndex)
                    if (path !== "") {
                        tagEditorDialog.nodePath = path
                        tagEditorDialog.open()
                    }
                }
                event.accepted = true
            } else if (event.key === Qt.Key_B && !ctrl && !shift) {
                // WS5: B - toggle favorite on focused item
                if (grid.currentIndex >= 0) {
                    const path = nodePathAt(grid.currentIndex)
                    if (path !== "" && favoritesController.toggleFavorite(path)) {
                        galleryModel.refresh()  // update the gold-star badge
                    }
                }
                event.accepted = true
            } else if (event.key === Qt.Key_R && !ctrl && !shift) {
                // WS5: R - rename focused item
                if (renameDialog && grid.currentIndex >= 0) {
                    const name = galleryModel.nameAt(grid.currentIndex)
                    if (name !== "") {
                        renameDialog.originalName = name
                        renameDialog.targetRow = grid.currentIndex
                        renameDialog.open()
                    }
                }
                event.accepted = true
            }
        }
    }

    // WS2 Task 2.2: Footer showing selection count
    // WS2 Task 2.3: Footer respects detail panel width (reflows with grid)
    Rectangle {
        id: galleryFooter
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: detailPanel.left
        }
        height: selectionController.count > 0 ? 40 : 0
        color: themePalette.surface
        border.color: themePalette.border
        border.width: 1
        visible: height > 0

        Text {
            anchors {
                verticalCenter: parent.verticalCenter
                left: parent.left
                leftMargin: 16
            }
            text: {
                const count = selectionController.count
                if (count === 1) return "1 item selected"
                return count + " items selected"
            }
            color: themePalette.text
            font.pixelSize: 14
        }
    }

    // WS2 Task 2.3: Detail panel showing metadata/tags for selected item
    DetailPanel {
        id: detailPanel
    }

    // WS2 Task 2.3: Wire detail panel to show selected node's details
    Connections {
        target: grid
        // T3.1 W5: a model reset (unlock, refresh) leaves currentIndex at -1 and
        // no currentPathChanged fires — restore or default the focused tile so
        // per-tile keys (B favorite, G tags, R rename) have a target.
        function onCountChanged() {
            if (grid.currentIndex < 0 && grid.count > 0) {
                const saved = sessionState.recallFocusIndex(galleryModel.currentPath)
                grid.currentIndex = (saved >= 0 && saved < grid.count) ? saved : 0
            }
        }
        function onCurrentIndexChanged() {
            if (grid.currentIndex >= 0 && grid.currentIndex < grid.model.count) {
                // Get the node key from the current item
                const nodeKey = grid.model.data(grid.model.index(grid.currentIndex, 0), GalleryModel.NodeKeyRole)
                // Show node with empty inherited tags and from-contents for now (TODO: compute from vault)
                detailController.showNode(nodeKey, [], [])

                // WS2 Task 2.5: Record the focused tile index for this path
                sessionState.recordFocusIndex(galleryModel.currentPath, grid.currentIndex)
            } else {
                detailController.clear()
            }
        }
    }

    // WS2 Task 2.5: Restore focused tile index when gallery changes
    Connections {
        target: galleryModel
        function onCurrentPathChanged() {
            // Recall the saved focus index for this path
            const savedIndex = sessionState.recallFocusIndex(galleryModel.currentPath)
            if (savedIndex >= 0 && savedIndex < grid.model.count) {
                grid.currentIndex = savedIndex
                grid.positionViewAtIndex(savedIndex, GridView.Contain)
            } else if (grid.model.count > 0) {
                // Default to first item if no saved index
                grid.currentIndex = 0
                grid.positionViewAtIndex(0, GridView.Beginning)
            }
        }
    }
}
