import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: window
    // These are initial dimensions only. Live size bindings reissue resize
    // requests after playback text changes and conflict with Wayland tiling.
    width: 1040; height: 700
    minimumWidth: dp(620)
    // Size hints may follow the font, but never transient player content.
    minimumHeight: dp(520)
    visible: true
    title: "Micpad"
    font: theme.uiFont
    color: surface
    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: accent
    Material.primary: surface
    Material.background: surface
    Material.foreground: ink
    Material.roundedScale: theme.radius
    palette.window: surface
    palette.windowText: ink
    palette.base: paper
    palette.alternateBase: alternatePaper
    palette.text: ink
    palette.button: hoverFill
    palette.buttonText: ink
    palette.highlight: selection
    palette.highlightedText: selectionInk
    palette.placeholderText: mutedInk
    palette.toolTipBase: surface
    palette.toolTipText: ink

    FontMetrics { id: textMetrics; font: window.font }
    readonly property real uiScale: Math.max(1,textMetrics.height/18)
    readonly property int rowHeight: Math.ceil(textMetrics.height+dp(12))
    readonly property int headingHeight: Math.ceil(textMetrics.height+dp(14))
    readonly property int percentageWidth: Math.ceil(textMetrics.advanceWidth(volumeText(1))+dp(8))
    readonly property bool fileColumnVisible: showFileNames && library.width >= dp(740)
    function dp(value) { return Math.round(value*uiScale) }

    readonly property bool darkMode: theme.dark
    property bool showFileNames: true
    readonly property color surface: theme.colors.background
    readonly property color paper: theme.colors.dark_background
    readonly property color ink: theme.colors.foreground
    readonly property color mutedInk: theme.colors.light_foreground
    readonly property color rule: theme.colors.muted
    readonly property color selection: theme.colors.selection
    readonly property color selectionInk: theme.colors.selection_foreground
    readonly property color hoverFill: theme.colors.lighter_background
    readonly property color accent: theme.accent
    readonly property color red: theme.colors.red
    readonly property color green: theme.colors.green
    readonly property color yellow: theme.colors.yellow
    readonly property color blue: theme.colors.blue
    readonly property color alternatePaper: Qt.tint(paper, Qt.rgba(ink.r,ink.g,ink.b,.025))
    property int selected: -1
    property string sortColumn: "number"
    property bool sortAscending: true
    property string librarySignature: ""
    readonly property bool dialogOpen: preferences.visible || help.visible || renameDialog.visible
    readonly property bool typing: search.activeFocus || renameField.activeFocus
    readonly property bool hasSelection: selected >= 0 && selected < backend.clips.length
    readonly property int visibleCount: soundRows.count
    readonly property int numberWidth: Math.max(dp(44),Math.ceil(textMetrics.advanceWidth("9999"))+dp(12))
    readonly property int hotkeyWidth: Math.max(dp(96),Math.ceil(textMetrics.advanceWidth(qsTr("Hotkey")))+dp(20))
    readonly property int fileWidth: fileColumnVisible ? Math.max(dp(200), library.width * .29) : 0
    readonly property int nameWidth: Math.max(dp(120), library.width - numberWidth - hotkeyWidth - fileWidth)

    function modelIndex(items, field, value) {
        for (let i=0; i<items.length; ++i)
            if (items[i][field] === value) return i
        return -1
    }
    function volumeText(value) {
        const language = translator.language
        return (value*100).toLocaleString(Qt.locale(), 'f', 1) + "%"
    }
    function rowForIndex(index) {
        for (let i=0; i<soundRows.count; ++i)
            if (soundRows.get(i).originalIndex === index) return i
        return -1
    }
    function rebuildRows() {
        let rows = []
        const query = search.text.trim().toLocaleLowerCase()
        const clips = backend.clips
        for (let i=0; i<clips.length; ++i) {
            const clip = clips[i]
            const fileName = clip.path.split('/').pop()
            if (query && !(clip.name + " " + fileName).toLocaleLowerCase().includes(query)) continue
            rows.push({originalIndex:i, soundName:clip.name, fileName:fileName, filePath:clip.path})
        }
        rows.sort(function(a,b) {
            let result = sortColumn === "name" ? a.soundName.localeCompare(b.soundName)
                       : sortColumn === "file" ? a.fileName.localeCompare(b.fileName)
                       : a.originalIndex - b.originalIndex
            return sortAscending ? result : -result
        })
        soundRows.clear()
        for (let row of rows) soundRows.append(row)
        if (rowForIndex(selected) < 0) selected = rows.length ? rows[0].originalIndex : -1
    }
    function syncLibrary() {
        const signature = JSON.stringify(backend.clips)
        if (signature === librarySignature) return
        librarySignature = signature
        rebuildRows()
    }
    function changeSort(column) {
        if (sortColumn === column) sortAscending = !sortAscending
        else { sortColumn = column; sortAscending = true }
        rebuildRows()
    }
    function moveSelection(delta, play) {
        if (!soundRows.count) return
        let row = rowForIndex(selected)
        row = Math.max(0,Math.min(soundRows.count-1,row < 0 ? 0 : row+delta))
        selected = soundRows.get(row).originalIndex
        library.positionViewAtIndex(row,ListView.Contain)
        if (play) backend.play(selected)
    }
    function renameSelected() {
        if (!hasSelection) return
        renameDialog.clipIndex = selected
        renameField.text = backend.clips[selected].name
        renameDialog.open()
        renameField.forceActiveFocus()
        renameField.selectAll()
    }
    Component.onCompleted: syncLibrary()
    Connections { target: backend; function onChanged() { window.syncLibrary() } }
    ListModel { id: soundRows }

    Shortcut { sequence: "Q"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Escape"; enabled: !window.dialogOpen; context: Qt.ApplicationShortcut; onActivated: { backend.stop(); search.clear(); library.forceActiveFocus() } }
    Shortcut { sequence: "Space"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: backend.playing ? backend.stop() : backend.play(window.selected) }
    Shortcut { sequences: ["Return", "Enter"]; enabled: !window.dialogOpen; context: Qt.ApplicationShortcut; onActivated: { backend.play(window.selected); library.forceActiveFocus() } }
    Shortcut { sequence: "Ctrl+O"; enabled: !window.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.chooseFiles() }
    Shortcut { sequence: "Ctrl+F"; enabled: !window.dialogOpen; context: Qt.ApplicationShortcut; onActivated: { search.forceActiveFocus(); search.selectAll() } }
    Shortcut { sequence: "F2"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: window.renameSelected() }
    Shortcut { sequence: "Delete"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: backend.remove(window.selected) }
    Shortcut { sequence: "?"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: help.open() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut { sequence: "Up"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: window.moveSelection(-1,false) }
    Shortcut { sequence: "Down"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: window.moveSelection(1,false) }
    Repeater {
        model: 9
        Item {
            id: keyItem
            required property int index
            Shortcut { sequence: (keyItem.index+1).toString(); enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: { window.selected = keyItem.index; backend.play(keyItem.index) } }
        }
    }

    component PadToolTip: ToolTip {
        font: window.font
        delay: 500
        Material.theme: window.darkMode ? Material.Dark : Material.Light
        Material.foreground: window.ink
        background: Rectangle { color: window.surface; border.color: window.rule; radius: Math.min(3,theme.radius) }
    }
    component PadMenu: Menu {
        font: window.font
        Material.elevation: 0
        background: Rectangle { color: window.surface; border.color: window.rule; radius: theme.radius; implicitWidth: window.dp(200) }
    }
    component PadMenuItem: MenuItem {
        id: action
        Material.foreground: window.ink
        background: Rectangle {
            implicitWidth: window.dp(200); implicitHeight: action.Material.menuItemHeight
            color: action.highlighted ? window.hoverFill : "transparent"
        }
    }
    component PadMenuSeparator: MenuSeparator {
        contentItem: Rectangle { implicitWidth: window.dp(200); implicitHeight: 1; color: window.rule }
    }
    component PadComboBox: ComboBox {
        id: combo
        Material.foreground: window.ink
        background: Rectangle {
            implicitWidth: window.dp(120); implicitHeight: combo.Material.textFieldHeight
            color: window.paper; border.color: combo.activeFocus ? window.accent : window.rule
            radius: Math.min(4,theme.radius)
        }
        popup.background: Rectangle { color: window.surface; border.color: window.rule; radius: theme.radius }
        delegate: PadMenuItem {
            required property var model
            required property int index
            width: ListView.view.width
            text: model[combo.textRole]
            highlighted: combo.highlightedIndex === index
        }
    }
    component SmallButton: AbstractButton {
        id: control
        property string symbol: "play"
        property string hint: ""
        property color iconColor: window.ink
        implicitWidth: window.dp(36); implicitHeight: window.dp(34)
        hoverEnabled: true
        Accessible.name: hint
        contentItem: Item { PadIcon { symbol: control.symbol; ink: control.enabled ? control.iconColor : window.mutedInk; opacity: control.enabled ? 1 : .35; anchors.centerIn: parent; width: window.dp(20); height: window.dp(20) } }
        background: Rectangle {
            radius: Math.min(3,theme.radius)
            color: control.down || control.checked ? window.selection : control.hovered ? window.hoverFill : "transparent"
            border.color: control.down || control.checked || control.hovered ? window.accent : "transparent"
        }
        PadToolTip { visible: control.hovered && control.hint.length > 0; text: control.hint }
    }
    component VolumeControl: RowLayout {
        id: volumeControl
        property bool microphone: false
        Layout.fillWidth: true; Layout.preferredWidth: 1
        spacing: window.dp(8)
        PadIcon {
            symbol: volumeControl.microphone ? "microphone" : "headphones"
            ink: volumeControl.microphone ? window.red : window.blue
            Layout.preferredWidth: window.dp(20); Layout.preferredHeight: window.dp(20)
        }
        Label { text: volumeControl.microphone ? qsTr("Microphone") : qsTr("Headphones"); color: window.mutedInk }
        Slider {
            id: volumeSlider
            objectName: volumeControl.microphone ? "microphoneVolumeSlider" : "headphoneVolumeSlider"
            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: window.dp(90)
            implicitHeight: window.dp(36)
            from: 0; to: 1; stepSize: .001
            value: volumeControl.microphone ? backend.microphoneVolume : backend.headphoneVolume
            onMoved: {
                if (volumeControl.microphone) backend.microphoneVolume = value
                else backend.headphoneVolume = value
            }
            Accessible.name: volumeControl.microphone ? qsTr("Microphone effect volume") : qsTr("Headphone effect volume")
            PadToolTip {
                visible: volumeSlider.hovered; delay: 600
                text: volumeControl.microphone ? qsTr("Effect volume sent to others. Your voice volume stays unchanged.") : qsTr("Headphone effect volume")
            }
        }
        Label {
            objectName: volumeControl.microphone ? "microphoneVolumeLabel" : "headphoneVolumeLabel"
            text: window.volumeText(volumeControl.microphone ? backend.microphoneVolume : backend.headphoneVolume)
            color: window.ink
            Layout.preferredWidth: window.percentageWidth
            horizontalAlignment: Text.AlignRight
        }
    }
    component Separator: Rectangle {
        implicitWidth: 1; implicitHeight: window.dp(18); color: window.rule
        Layout.leftMargin: window.dp(5); Layout.rightMargin: window.dp(5)
    }
    component ColumnHeading: AbstractButton {
        id: heading
        property string column: ""
        property bool centered: false
        implicitHeight: window.headingHeight
        hoverEnabled: true
        contentItem: Text {
            text: heading.text + (window.sortColumn === heading.column ? (window.sortAscending ? "  ▴" : "  ▾") : "")
            color: window.ink; font: window.font
            leftPadding: heading.centered ? 0 : window.dp(12)
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: heading.centered ? Text.AlignHCenter : Text.AlignLeft
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: heading.hovered ? window.hoverFill : window.surface
            Rectangle { anchors.right: parent.right; height: parent.height; width: 1; color: window.rule }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: window.rule }
        }
        onClicked: window.changeSort(column)
    }
    component PadDialog: Dialog {
        id: popup
        font: window.font
        padding: window.dp(12)
        topPadding: window.dp(8)
        Material.roundedScale: theme.radius
        Material.elevation: 0
        background: Rectangle { color: window.surface; border.color: window.rule; radius: theme.radius }
        header: Label { text: popup.title; color: window.ink; font.bold: true; padding: window.dp(12); bottomPadding: window.dp(4) }
        footer: DialogButtonBox {
            visible: count > 0
            Material.roundedScale: theme.radius
            background: Rectangle { color: window.surface }
            delegate: Button { flat: true; implicitHeight: window.dp(30); topInset: 0; bottomInset: 0; Material.roundedScale: theme.radius }
        }
        Overlay.modal: Rectangle { color: "#80000000" }
    }

    menuBar: MenuBar {
        id: mainMenu
        implicitHeight: window.headingHeight
        background: Rectangle { color: window.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: window.rule } }
        delegate: MenuBarItem {
            id: menuEntry
            implicitHeight: window.headingHeight
            contentItem: Text { text: menuEntry.text; color: window.ink; font: window.font; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
            background: Rectangle { color: menuEntry.highlighted ? window.hoverFill : "transparent"; border.color: menuEntry.highlighted ? window.accent : "transparent" }
        }
        PadMenu {
            title: qsTr("File")
            PadMenuItem { text: qsTr("Add sound files…"); onTriggered: backend.chooseFiles() }
            PadMenuSeparator {}
            PadMenuItem { text: qsTr("Preferences…"); onTriggered: preferences.open() }
            PadMenuSeparator {}
            PadMenuItem { text: qsTr("Exit"); onTriggered: Qt.quit() }
        }
        PadMenu {
            title: qsTr("Edit")
            PadMenuItem { text: qsTr("Undo"); onTriggered: backend.undo() }
            PadMenuItem { text: qsTr("Redo"); onTriggered: backend.redo() }
            PadMenuSeparator {}
            PadMenuItem { text: qsTr("Rename"); enabled: window.hasSelection; onTriggered: window.renameSelected() }
            PadMenuItem { text: qsTr("Remove from library"); enabled: window.hasSelection; onTriggered: backend.remove(window.selected) }
            PadMenuSeparator {}
            PadMenuItem { text: qsTr("Find…"); onTriggered: search.forceActiveFocus() }
        }
        PadMenu {
            title: qsTr("Play")
            PadMenuItem { text: qsTr("Play selected sound"); enabled: window.hasSelection; onTriggered: backend.play(window.selected) }
            PadMenuItem { text: qsTr("Play on speakers"); enabled: window.hasSelection; onTriggered: backend.preview(window.selected) }
            PadMenuItem { text: qsTr("Stop"); onTriggered: backend.stop() }
            PadMenuSeparator {}
            PadMenuItem { text: qsTr("Previous sound"); enabled: soundRows.count > 0; onTriggered: window.moveSelection(-1,true) }
            PadMenuItem { text: qsTr("Next sound"); enabled: soundRows.count > 0; onTriggered: window.moveSelection(1,true) }
            PadMenuSeparator {}
            PadMenuItem { text: qsTr("Repeat current sound"); checkable: true; checked: backend.repeat; onTriggered: backend.repeat = !backend.repeat }
            PadMenuItem { text: qsTr("Hear played sounds myself"); checkable: true; checked: backend.monitorEnabled; onTriggered: backend.monitorEnabled = !backend.monitorEnabled }
        }
        PadMenu {
            title: qsTr("View")
            PadMenuItem { text: qsTr("File names"); checkable: true; checked: window.showFileNames; onTriggered: window.showFileNames = !window.showFileNames }
        }
        PadMenu {
            title: qsTr("Help")
            PadMenuItem { text: qsTr("Keyboard shortcuts"); onTriggered: help.open() }
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) { if (drop.hasUrls) backend.addUrls(drop.urls) }
    }
    ColumnLayout {
        id: contentLayout
        anchors.fill: parent; spacing: 0
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        RowLayout {
            Layout.fillWidth: true; Layout.margins: window.dp(4); spacing: 1
            SmallButton { symbol: "add"; iconColor: window.yellow; hint: qsTr("Add sound files…") + " (Ctrl+O)"; onClicked: backend.chooseFiles() }
            Separator {}
            SmallButton { symbol: "previous"; hint: qsTr("Previous sound"); enabled: soundRows.count > 0; onClicked: window.moveSelection(-1,true) }
            SmallButton { objectName: "playButton"; symbol: "play"; iconColor: window.green; hint: qsTr("Play selected sound") + " (Enter)"; enabled: window.hasSelection; onClicked: backend.play(window.selected) }
            SmallButton { symbol: "stop"; hint: qsTr("Stop") + " (Esc)"; onClicked: backend.stop() }
            SmallButton { symbol: "next"; hint: qsTr("Next sound"); enabled: soundRows.count > 0; onClicked: window.moveSelection(1,true) }
            Separator {}
            SmallButton { symbol: "headphones"; iconColor: window.blue; hint: qsTr("Play on speakers"); enabled: window.hasSelection; onClicked: backend.preview(window.selected) }
            SmallButton { symbol: "microphone"; iconColor: window.red; hint: qsTr("Send to microphone"); enabled: window.hasSelection; onClicked: backend.play(window.selected) }
            Separator {}
            SmallButton { symbol: "repeat"; hint: qsTr("Repeat current sound"); checked: backend.repeat; onClicked: backend.repeat = !backend.repeat }
            Item { Layout.fillWidth: true }
            SmallButton { objectName: "preferencesButton"; symbol: "settings"; hint: qsTr("Preferences…"); onClicked: preferences.open() }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: window.rule }
        GridLayout {
            id: volumeGrid
            objectName: "volumeGrid"
            Layout.fillWidth: true
            Layout.leftMargin: window.dp(10); Layout.rightMargin: window.dp(10)
            columns: width < window.dp(850) ? 1 : 2
            columnSpacing: window.dp(24); rowSpacing: window.dp(2)
            VolumeControl { microphone: true }
            VolumeControl { microphone: false }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: window.rule }
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: window.dp(10); Layout.rightMargin: window.dp(10); Layout.topMargin: window.dp(5); Layout.bottomMargin: window.dp(5); spacing: window.dp(10)
            Label { Layout.preferredWidth: window.dp(160); text: backend.busy ? qsTr("Loading…") : backend.playing || qsTr("Ready"); color: window.ink; elide: Text.ElideRight }
            ProgressBar {
                id: playbackProgress
                Layout.fillWidth: true; Layout.preferredHeight: window.dp(8)
                value: backend.progress; indeterminate: backend.busy
                background: Rectangle { implicitHeight: window.dp(8); color: theme.colors.darker_background; border.color: window.rule }
                contentItem: Item { Rectangle { width: backend.busy ? parent.width*.2 : playbackProgress.visualPosition*parent.width; height: parent.height; color: window.accent } }
            }
            Label {
                visible: backend.playing.length > 0 && !backend.busy
                text: backend.sending
                    ? (backend.listening ? qsTr("Microphone + headphones") : qsTr("Microphone"))
                    : (backend.listening ? qsTr("Headphones only") : qsTr("Connecting…"))
                color: window.mutedInk
            }
            Label { text: Math.round(backend.progress*100) + "%"; color: window.mutedInk; Layout.preferredWidth: window.dp(32); horizontalAlignment: Text.AlignRight }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: window.rule }
        RowLayout {
            Layout.fillWidth: true; Layout.margins: window.dp(5); spacing: window.dp(6)
            PadIcon { symbol: "search"; ink: window.mutedInk; Layout.preferredWidth: window.dp(15); Layout.preferredHeight: window.dp(15) }
            TextField {
                id: search; objectName: "searchField"
                Layout.fillWidth: true; implicitHeight: window.headingHeight
                placeholderText: qsTr("Search sounds…")
                color: window.ink; placeholderTextColor: window.mutedInk
                selectionColor: window.selection; selectedTextColor: window.selectionInk
                leftPadding: window.dp(7); rightPadding: window.dp(7); topPadding: window.dp(3); bottomPadding: window.dp(3)
                selectByMouse: true
                background: Rectangle { color: window.paper; border.color: search.activeFocus ? window.accent : window.rule; radius: Math.min(2,theme.radius) }
                onTextChanged: window.rebuildRows()
            }
            SmallButton { symbol: "close"; implicitWidth: window.dp(34); implicitHeight: window.dp(34); hint: qsTr("Clear search"); enabled: search.text.length > 0; onClicked: { search.clear(); library.forceActiveFocus() } }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: window.headingHeight+window.rowHeight*3+2
            Layout.leftMargin: window.dp(5); Layout.rightMargin: window.dp(5)
            color: window.paper; border.color: window.rule
            Column {
                anchors.fill: parent; anchors.margins: 1
                Row {
                    id: headings
                    width: parent.width; height: window.headingHeight
                    ColumnHeading { width: window.numberWidth; text: "#"; centered: true; column: "number" }
                    ColumnHeading { width: window.nameWidth; text: qsTr("Name"); column: "name" }
                    ColumnHeading { width: window.hotkeyWidth; text: qsTr("Hotkey"); column: "hotkey" }
                    ColumnHeading { width: window.fileWidth; visible: window.fileColumnVisible; text: qsTr("File"); column: "file" }
                }
                ListView {
                    id: library; objectName: "soundTable"
                    width: parent.width; height: Math.max(0,parent.height-headings.height)
                    clip: true; model: soundRows; boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: soundRow
                        required property int index
                        required property int originalIndex
                        required property string soundName
                        required property string fileName
                        required property string filePath
                        width: library.width; height: window.rowHeight
                        color: window.selected === originalIndex ? window.selection : rowMouse.containsMouse ? window.hoverFill : (index%2 ? window.alternatePaper : window.paper)
                        border.color: window.selected === originalIndex ? window.accent : "transparent"
                        readonly property color rowInk: window.selected === originalIndex ? window.selectionInk : window.ink
                        readonly property color rowMutedInk: window.selected === originalIndex ? window.selectionInk : window.mutedInk
                        Row {
                            anchors.fill: parent
                            Text { width: window.numberWidth; height: parent.height; text: soundRow.originalIndex+1; color: soundRow.rowMutedInk; font: window.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            Item {
                                width: window.nameWidth; height: parent.height
                                PadIcon { x: window.dp(9); anchors.verticalCenter: parent.verticalCenter; width: window.dp(13); height: window.dp(13); symbol: "sound"; ink: window.blue }
                                Text { x: window.dp(30); width: parent.width-window.dp(38); height: parent.height; text: soundRow.soundName; color: soundRow.rowInk; font: window.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                            }
                            Text { width: window.hotkeyWidth; height: parent.height; leftPadding: window.dp(10); text: soundRow.originalIndex < 9 ? (soundRow.originalIndex+1).toString() : ""; color: soundRow.rowMutedInk; font: window.font; verticalAlignment: Text.AlignVCenter }
                            Text { width: window.fileWidth; height: parent.height; visible: window.fileColumnVisible; leftPadding: window.dp(10); rightPadding: window.dp(8); text: soundRow.fileName; color: soundRow.rowMutedInk; font: window.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle }
                        }
                        MouseArea {
                            id: rowMouse
                            anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.LeftButton | Qt.RightButton
                            onClicked: function(mouse) {
                                window.selected = soundRow.originalIndex
                                library.forceActiveFocus()
                                if (mouse.button === Qt.RightButton) contextMenu.popup()
                            }
                            onDoubleClicked: function(mouse) { if (mouse.button === Qt.LeftButton) backend.play(soundRow.originalIndex) }
                        }
                        PadToolTip { visible: rowMouse.containsMouse && !contextMenu.opened; delay: 1000; text: soundRow.filePath }
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width-window.dp(32)
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        visible: soundRows.count === 0
                        text: backend.clips.length ? qsTr("No matching sounds") : qsTr("Drop audio files here or press Ctrl+O")
                        color: window.mutedInk
                    }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.leftMargin: window.dp(7); Layout.rightMargin: window.dp(7); Layout.topMargin: window.dp(5)
            implicitHeight: errorRow.implicitHeight+10
            visible: backend.error.length > 0
            color: Qt.tint(window.surface,Qt.rgba(window.red.r,window.red.g,window.red.b,.12)); border.color: window.red
            RowLayout {
                id: errorRow
                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 5; anchors.topMargin: 5; anchors.bottomMargin: 5
                Label { Layout.fillWidth: true; text: backend.error; color: window.ink; wrapMode: Text.WrapAnywhere; maximumLineCount: 2; elide: Text.ElideRight }
                SmallButton { symbol: "close"; implicitWidth: window.dp(30); implicitHeight: window.dp(30); hint: qsTr("Dismiss"); onClicked: backend.clearError() }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: window.dp(10); Layout.rightMargin: window.dp(10); Layout.topMargin: window.dp(4); Layout.bottomMargin: window.dp(4); spacing: window.dp(8)
            Label { text: qsTr("Sounds: %1").arg(backend.clips.length); color: window.mutedInk }
            Separator { implicitHeight: window.dp(15) }
            Label { Layout.fillWidth: true; text: window.hasSelection ? backend.clips[window.selected].path.split('/').pop() : backend.status; color: window.mutedInk; elide: Text.ElideMiddle }
            SmallButton { symbol: "repeat"; implicitWidth: window.dp(32); implicitHeight: window.dp(32); hint: qsTr("Repeat current sound"); checked: backend.repeat; onClicked: backend.repeat = !backend.repeat }
            SmallButton { symbol: "microphone"; implicitWidth: window.dp(32); implicitHeight: window.dp(32); iconColor: backend.connected ? (window.red) : window.mutedInk; hint: qsTr("Microphone and target app"); onClicked: preferences.open() }
        }
    }
    PadMenu {
        id: contextMenu
        objectName: "soundContextMenu"
        PadMenuItem { text: qsTr("Play selected sound"); onTriggered: backend.play(window.selected) }
        PadMenuItem { text: qsTr("Play on speakers"); onTriggered: backend.preview(window.selected) }
        PadMenuSeparator {}
        PadMenuItem { text: qsTr("Rename"); onTriggered: window.renameSelected() }
        PadMenuItem { text: qsTr("Remove from library"); onTriggered: backend.remove(window.selected) }
    }
    PadDialog {
        id: preferences; objectName: "preferencesDialog"
        anchors.centerIn: parent; width: Math.min(window.width-window.dp(40),window.dp(640))
        height: Math.min(window.height-window.dp(24),implicitHeight)
        title: qsTr("Preferences")
        modal: true; standardButtons: Dialog.Close
        contentItem: ScrollView {
            id: preferencesScroll
            implicitHeight: preferencesFields.implicitHeight
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
            id: preferencesFields
            width: preferencesScroll.availableWidth
            spacing: window.dp(8)
            CheckBox {
                objectName: "monitorCheckBox"
                Layout.fillWidth: true
                text: qsTr("Hear played sounds myself")
                checked: backend.monitorEnabled
                onToggled: backend.monitorEnabled = checked
            }
            Label { text: qsTr("Microphone"); color: window.ink; font.bold: true }
            PadComboBox {
                Layout.fillWidth: true
                model: backend.microphones; textRole: "label"; valueRole: "name"
                currentIndex: window.modelIndex(model,"name",backend.microphone)
                displayText: currentIndex < 0 ? qsTr("Select physical microphone") : currentText
                onActivated: backend.microphone = currentValue
            }
            Label { text: qsTr("Send sounds to"); color: window.ink; font.bold: true }
            PadComboBox {
                Layout.fillWidth: true
                model: backend.applications; textRole: "label"; valueRole: "serial"
                currentIndex: window.modelIndex(model,"serial",backend.target)
                displayText: currentIndex < 0 ? qsTr("Target app disconnected") : currentText
                onActivated: backend.target = currentValue
            }
            Label { Layout.fillWidth: true; text: backend.status; color: window.mutedInk; wrapMode: Text.WordWrap }
            Button { text: qsTr("Keyboard shortcuts…"); onClicked: { preferences.close(); help.open() } }
            }
        }
    }
    PadDialog {
        id: help
        anchors.centerIn: parent; width: Math.min(window.width-window.dp(40),window.dp(680))
        height: Math.min(window.height-window.dp(24),implicitHeight)
        title: qsTr("Keyboard shortcuts")
        modal: true; standardButtons: Dialog.Close
        contentItem: ScrollView {
            id: helpScroll
            implicitHeight: helpFields.implicitHeight
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
            id: helpFields
            width: helpScroll.availableWidth
            spacing: window.dp(14)
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Enter or double-click plays the selected sound. Space plays/stops, Esc stops, ↑/↓ selects a sound. Ctrl+F searches, Ctrl+O adds files, F2 renames, Delete removes from the list, Ctrl+Z undoes changes.") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Keys 1–9 play sounds by their original list number, even after sorting. Global keys Ctrl+Alt+1–9 work in other apps after enabling them; Ctrl+Alt+0 stops playback.") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Ctrl+Shift+Z redoes changes. Q quits. ? opens this help.") }
            Button { text: qsTr("Enable global shortcuts"); onClicked: { backend.enableHotkeys(); help.close() } }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: window.mutedInk; text: qsTr("You can listen without a call. To send sounds to others, keep the same microphone selected in the calling app. Microphone mute stops sending to others while local music keeps playing.") }
            }
        }
    }
    PadDialog {
        id: renameDialog
        property int clipIndex: -1
        anchors.centerIn: parent; width: window.dp(360)
        title: qsTr("Rename sound"); modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: TextField { id: renameField; objectName: "renameField"; selectByMouse: true; onAccepted: renameDialog.accept() }
        onAccepted: backend.rename(clipIndex,renameField.text)
        onClosed: library.forceActiveFocus()
    }
}
