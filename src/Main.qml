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
    minimumHeight: dp(620)
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
    FontMetrics { id: monoMetrics; font: window.monoFont }
    readonly property font monoFont: theme.consoleFont
    readonly property font monoBoldFont: styledFont(monoFont,true,0)
    function styledFont(base, bold, spacing) {
        let face = {family: base.family, bold: bold, letterSpacing: spacing}
        if (base.pointSize > 0) face.pointSize = base.pointSize
        else face.pixelSize = base.pixelSize
        return Qt.font(face)
    }
    readonly property real uiScale: Math.max(1,textMetrics.height/18)
    readonly property int rowHeight: Math.ceil(textMetrics.height+dp(20))
    readonly property int headingHeight: Math.ceil(textMetrics.height+dp(14))
    readonly property int channelLabelWidth: Math.ceil(Math.max(textMetrics.advanceWidth(qsTr("Microphone")),textMetrics.advanceWidth(qsTr("Headphones"))))+dp(4)
    readonly property int percentageWidth: Math.ceil(monoMetrics.advanceWidth(volumeText(1))+dp(8))
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
    readonly property color accentInk: theme.accentForeground
    readonly property color red: theme.colors.red
    readonly property color green: theme.colors.green
    readonly property color yellow: theme.colors.yellow
    readonly property color blue: theme.colors.blue
    readonly property color alternatePaper: Qt.tint(paper, Qt.rgba(ink.r,ink.g,ink.b,.025))
    readonly property string playbackState: backend.busy ? qsTr("Loading…") : !backend.playing ? qsTr("Ready")
        : backend.paused ? qsTr("Paused")
        : backend.sending ? (backend.listening ? qsTr("Microphone + headphones") : qsTr("Microphone"))
        : backend.listening ? qsTr("Headphones only") : qsTr("Connecting…")
    property int selected: -1
    property string sortColumn: "number"
    property bool sortAscending: true
    property string librarySignature: ""
    readonly property bool dialogOpen: preferences.visible || help.visible || renameDialog.visible || appMenu.visible || contextMenu.visible
    readonly property bool adjustingLevels: microphoneControl.adjusting || headphoneControl.adjusting
    readonly property string errorMessage: backend.error || translator.error || theme.error
    readonly property bool typing: search.activeFocus || renameField.activeFocus
    readonly property bool hasSelection: selected >= 0 && selected < backend.clips.length
    readonly property int visibleCount: soundRows.count
    readonly property int numberWidth: Math.max(dp(44),Math.ceil(textMetrics.advanceWidth("9999"))+dp(12))
    readonly property int hotkeyWidth: Math.max(dp(100),Math.ceil(monoMetrics.advanceWidth(qsTr("Hotkey")))+dp(24))
    readonly property int fileWidth: fileColumnVisible ? Math.max(dp(200), library.width * .27) : 0
    readonly property int nameWidth: Math.max(dp(120), library.width - numberWidth - hotkeyWidth - fileWidth)

    function modelIndex(items, field, value) {
        for (let i=0; i<items.length; ++i)
            if (items[i][field] === value) return i
        return -1
    }
    function volumeText(value) {
        const language = translator.language
        return (value*100).toLocaleString(Qt.locale(), 'f', 0) + "%"
    }
    function timeText(value) {
        const seconds = Math.max(0,Math.floor(value || 0))
        const minutes = Math.floor(seconds/60)
        return minutes >= 60 ? Math.floor(minutes/60)+":"+(minutes%60).toString().padStart(2,"0")+":"+(seconds%60).toString().padStart(2,"0")
            : minutes+":"+(seconds%60).toString().padStart(2,"0")
    }
    function togglePlayback() {
        if (backend.busy) return
        if (backend.playing.length) backend.paused = !backend.paused
        else backend.play(window.selected)
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
    Shortcut { sequence: "Space"; enabled: !window.dialogOpen && !window.typing && !window.adjustingLevels; context: Qt.ApplicationShortcut; onActivated: window.togglePlayback() }
    Shortcut { sequence: "Left"; enabled: !window.dialogOpen && !window.typing && !window.adjustingLevels && !playbackProgress.activeFocus; context: Qt.ApplicationShortcut; onActivated: backend.seekBy(-5) }
    Shortcut { sequence: "Right"; enabled: !window.dialogOpen && !window.typing && !window.adjustingLevels && !playbackProgress.activeFocus; context: Qt.ApplicationShortcut; onActivated: backend.seekBy(5) }
    Shortcut { sequences: ["Return", "Enter"]; enabled: !window.dialogOpen; context: Qt.ApplicationShortcut; onActivated: { backend.play(window.selected); library.forceActiveFocus() } }
    Shortcut { sequence: "Ctrl+O"; enabled: !window.dialogOpen; context: Qt.ApplicationShortcut; onActivated: backend.chooseFiles() }
    Shortcut { sequence: "Ctrl+F"; enabled: !window.dialogOpen; context: Qt.ApplicationShortcut; onActivated: { search.forceActiveFocus(); search.selectAll() } }
    Shortcut { sequence: "F2"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: window.renameSelected() }
    Shortcut { sequence: "Delete"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: backend.remove(window.selected) }
    Shortcut { sequence: "?"; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: help.open() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: backend.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: backend.redo() }
    Shortcut { sequence: "Up"; enabled: !window.dialogOpen && !window.typing && !window.adjustingLevels && !playbackProgress.activeFocus; context: Qt.ApplicationShortcut; onActivated: window.moveSelection(-1,false) }
    Shortcut { sequence: "Down"; enabled: !window.dialogOpen && !window.typing && !window.adjustingLevels && !playbackProgress.activeFocus; context: Qt.ApplicationShortcut; onActivated: window.moveSelection(1,false) }
    Repeater {
        model: 9
        Item {
            id: keyItem
            required property int index
            Shortcut { sequence: (keyItem.index+1).toString(); enabled: !window.dialogOpen && !window.typing; context: Qt.ApplicationShortcut; onActivated: { window.selected = keyItem.index; backend.play(keyItem.index) } }
        }
    }

    component KeyCap: Rectangle {
        id: cap
        property string label: ""
        property color labelColor: window.mutedInk
        implicitWidth: capText.implicitWidth+window.dp(12)
        implicitHeight: capText.implicitHeight+window.dp(4)
        color: "transparent"; border.color: window.rule; radius: Math.min(2,theme.radius)
        Text { id: capText; anchors.centerIn: parent; text: cap.label; font: window.monoFont; color: cap.labelColor }
    }
    component SectionTitle: RowLayout {
        property string number: "01"
        property string caption: ""
        spacing: window.dp(8)
        Label { text: "["+parent.number+"]"; color: window.accent; font: window.monoFont }
        Label { text: parent.caption.toLocaleUpperCase(); color: window.ink; font: window.monoBoldFont }
        Rectangle { Layout.fillWidth: true; height: 1; color: window.rule; Layout.leftMargin: window.dp(4) }
    }
    component RouteDiagram: Item {
        id: diagram
        readonly property bool microphoneActive: backend.playing.length > 0 && !backend.busy && !backend.paused && backend.sending
        readonly property bool headphoneActive: backend.playing.length > 0 && !backend.busy && !backend.paused && backend.listening
        readonly property color microphoneInk: microphoneActive ? window.red : window.rule
        readonly property color headphoneInk: headphoneActive ? window.blue : window.rule
        readonly property color feedInk: microphoneActive && headphoneActive ? window.accent : microphoneActive ? window.red : headphoneActive ? window.blue : window.rule
        readonly property color sourceInk: backend.paused ? window.accent : feedInk
        implicitWidth: window.dp(82); implicitHeight: window.dp(36)
        Accessible.role: Accessible.StaticText
        Accessible.name: window.playbackState
        Rectangle { x: window.dp(2); y: window.dp(10); width: window.dp(14); height: window.dp(14); color: "transparent"; border.color: diagram.sourceInk
            PadIcon { anchors.fill: parent; anchors.margins: window.dp(2); symbol: backend.paused ? "pause" : "play"; ink: diagram.sourceInk }
        }
        Rectangle { objectName: "routingFeed"; x: window.dp(16); y: window.dp(17); width: window.dp(30)-x; height: 1; color: diagram.feedInk }
        Rectangle { objectName: "microphoneStem"; x: window.dp(29); y: window.dp(8); width: 1; height: window.dp(17)-y; color: diagram.microphoneInk }
        Rectangle { objectName: "headphoneStem"; x: window.dp(29); y: window.dp(17)+1; width: 1; height: window.dp(26)-y+1; color: diagram.headphoneInk }
        Rectangle { objectName: "microphoneBranch"; x: window.dp(29); y: window.dp(8); width: window.dp(25); height: 1; color: diagram.microphoneInk }
        Rectangle { objectName: "headphoneBranch"; x: window.dp(29); y: window.dp(26); width: window.dp(25); height: 1; color: diagram.headphoneInk }
        PadIcon { objectName: "microphoneRouteIndicator"; x: window.dp(59); y: 0; width: window.dp(16); height: window.dp(16); symbol: "microphone"; ink: diagram.microphoneInk }
        PadIcon { objectName: "headphoneRouteIndicator"; x: window.dp(59); y: window.dp(19); width: window.dp(16); height: window.dp(16); symbol: "headphones"; ink: diagram.headphoneInk }
        HoverHandler { id: diagramHover }
        PadToolTip { visible: diagramHover.hovered; text: window.playbackState }
    }
    component PadToolTip: ToolTip {
        id: tip
        font: window.monoFont
        delay: 500
        Material.theme: window.darkMode ? Material.Dark : Material.Light
        Material.foreground: window.ink
        contentItem: Text { text: tip.text; font: tip.font; color: window.ink; wrapMode: Text.Wrap }
        background: Rectangle { color: window.surface; border.color: window.accent; radius: Math.min(3,theme.radius) }
    }
    component PadButton: Button {
        id: control
        property string iconSymbol: ""
        property string hint: ""
        property string keyHint: ""
        property bool primary: false
        property color iconColor: window.ink
        readonly property color textInk: !enabled ? window.mutedInk : primary ? window.accentInk : checked || down ? window.selectionInk : window.ink
        implicitWidth: Math.max(window.dp(36),buttonContent.implicitWidth+leftPadding+rightPadding)
        implicitHeight: Math.max(window.dp(36),textMetrics.height+window.dp(14))
        padding: window.dp(10); topPadding: window.dp(6); bottomPadding: window.dp(6)
        // Material declares its own horizontal paddings; setting padding
        // alone leaves their asymmetric values in effect for custom icons.
        leftPadding: padding; rightPadding: padding
        leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0
        hoverEnabled: true
        font: window.font
        Accessible.name: text || hint
        contentItem: Item {
            id: buttonContent
            implicitWidth: (control.iconSymbol.length ? window.dp(18) : 0)
                + (control.text.length ? buttonLabel.implicitWidth : 0)
                + (control.keyHint.length ? buttonKey.implicitWidth : 0)
                + window.dp(8)*Math.max(0,(control.iconSymbol.length > 0 ? 1 : 0)+(control.text.length > 0 ? 1 : 0)+(control.keyHint.length > 0 ? 1 : 0)-1)
            implicitHeight: Math.max(window.dp(18),buttonLabel.implicitHeight)
            Row {
                anchors.centerIn: parent; spacing: window.dp(8)
                PadIcon {
                    objectName: "buttonGlyph"
                    visible: control.iconSymbol.length > 0
                    width: window.dp(18); height: window.dp(18); anchors.verticalCenter: parent.verticalCenter
                    symbol: control.iconSymbol; ink: control.primary ? control.textInk : control.iconColor; opacity: control.enabled ? 1 : .35
                }
                Text {
                    id: buttonLabel
                    visible: control.text.length > 0
                    text: control.text; font: control.font; color: control.textInk
                    width: Math.min(implicitWidth,Math.max(0,control.availableWidth-(control.iconSymbol.length ? window.dp(26) : 0)-(control.keyHint.length ? buttonKey.implicitWidth+window.dp(8) : 0)))
                    anchors.verticalCenter: parent.verticalCenter
                    verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                }
                KeyCap { id: buttonKey; visible: control.keyHint.length > 0; label: control.keyHint; labelColor: control.textInk; opacity: .7; anchors.verticalCenter: parent.verticalCenter }
            }
        }
        background: Rectangle {
            radius: Math.min(3,theme.radius)
            color: control.primary ? window.accent : control.down || control.checked ? window.selection : control.hovered ? window.hoverFill : window.paper
            border.color: control.primary || control.visualFocus || control.hovered || control.checked ? window.accent : window.rule
            opacity: control.enabled ? 1 : .45
            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: control.down ? 0 : 2; color: control.primary ? window.accentInk : window.rule; opacity: .2 }
        }
        PadToolTip { visible: control.hovered && control.hint.length > 0; text: control.hint }
    }
    component SmallButton: PadButton {
        property string symbol: "play"
        iconSymbol: symbol
        implicitWidth: window.dp(36); padding: window.dp(8)
    }
    component PadCheckBox: CheckBox {
        id: control
        font: window.font
        spacing: window.dp(9); padding: 0
        implicitHeight: Math.max(window.dp(26),textMetrics.height+window.dp(6))
        indicator: Rectangle {
            x: 0; y: (control.height-height)/2
            implicitWidth: window.dp(17); implicitHeight: window.dp(17)
            color: control.checked ? window.accent : window.paper
            border.color: control.hovered || control.checked || control.visualFocus ? window.accent : window.rule
            radius: Math.min(2,theme.radius)
            PadIcon { anchors.fill: parent; anchors.margins: window.dp(3); symbol: "check"; ink: window.accentInk; visible: control.checked }
        }
        contentItem: Text {
            text: control.text; font: control.font; color: window.ink
            leftPadding: control.indicator.width+control.spacing
            verticalAlignment: Text.AlignVCenter; wrapMode: Text.WordWrap
        }
        background: Rectangle { color: "transparent" }
    }
    component PadMenu: Menu {
        font: window.font
        padding: window.dp(5)
        Material.elevation: 0
        background: Rectangle { color: window.surface; border.color: window.accent; radius: theme.radius; implicitWidth: window.dp(220) }
    }
    component PadMenuItem: MenuItem {
        id: action
        padding: window.dp(10)
        implicitHeight: textMetrics.height+window.dp(16)
        implicitWidth: Math.max(window.dp(220),implicitContentWidth+leftPadding+rightPadding)
        Material.foreground: window.ink
        contentItem: Text {
            text: action.text; font: window.font; color: action.enabled ? window.ink : window.mutedInk
            leftPadding: action.checkable ? window.dp(24) : 0
            verticalAlignment: Text.AlignVCenter
        }
        indicator: Rectangle {
            visible: action.checkable
            width: window.dp(12); height: window.dp(12)
            x: action.leftPadding; y: (action.height-height)/2
            color: action.checked ? window.accent : window.paper; border.color: window.accent
        }
        background: Rectangle { color: action.highlighted ? window.hoverFill : "transparent" }
    }
    component PadMenuSeparator: MenuSeparator {
        padding: window.dp(5)
        contentItem: Rectangle { implicitWidth: window.dp(200); implicitHeight: 1; color: window.rule }
    }
    component PadComboBox: ComboBox {
        id: combo
        implicitHeight: textMetrics.height+window.dp(20)
        leftPadding: window.dp(12); rightPadding: window.dp(36)
        Material.foreground: window.ink
        contentItem: Text { text: combo.displayText; color: window.ink; font: window.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        indicator: PadIcon { symbol: "chevron"; ink: window.mutedInk; width: window.dp(16); height: window.dp(16); x: combo.width-width-window.dp(12); y: (combo.height-height)/2 }
        background: Rectangle { color: window.paper; border.color: combo.activeFocus || combo.hovered ? window.accent : window.rule; radius: Math.min(3,theme.radius) }
        popup.background: Rectangle { color: window.surface; border.color: window.accent; radius: theme.radius }
        delegate: PadMenuItem {
            required property var model
            required property int index
            width: ListView.view.width
            text: model[combo.textRole]
            highlighted: combo.highlightedIndex === index
        }
    }
    component VolumeControl: RowLayout {
        id: volumeControl
        property bool microphone: false
        readonly property bool adjusting: volumeSlider.activeFocus
        Layout.fillWidth: true; Layout.preferredWidth: 1
        spacing: window.dp(8)
        PadIcon { symbol: volumeControl.microphone ? "microphone" : "headphones"; ink: volumeControl.microphone ? window.red : window.blue; Layout.preferredWidth: window.dp(18); Layout.preferredHeight: window.dp(18) }
        Label {
            objectName: volumeControl.microphone ? "microphoneChannelLabel" : "headphoneChannelLabel"
            text: volumeControl.microphone ? qsTr("Microphone") : qsTr("Headphones")
            color: window.mutedInk; font: window.font
            Layout.preferredWidth: Math.max(window.channelLabelWidth,implicitWidth)
        }
        Slider {
            id: volumeSlider
            objectName: volumeControl.microphone ? "microphoneVolumeSlider" : "headphoneVolumeSlider"
            Layout.fillWidth: true; Layout.preferredWidth: 1; Layout.minimumWidth: window.dp(110)
            implicitHeight: window.dp(28); leftPadding: window.dp(4); rightPadding: window.dp(4)
            from: 0; to: 1; stepSize: 1/backend.volumeSteps
            snapMode: Slider.SnapAlways
            value: volumeControl.microphone ? backend.microphoneVolume : backend.headphoneVolume
            onMoved: {
                if (volumeControl.microphone) backend.microphoneVolume = value
                else backend.headphoneVolume = value
            }
            Accessible.name: volumeControl.microphone ? qsTr("Microphone effect volume") : qsTr("Headphone effect volume")
            background: Item {
                id: rail
                x: volumeSlider.leftPadding+volumeSlider.handle.width/2; y: (volumeSlider.height-height)/2
                width: volumeSlider.availableWidth-volumeSlider.handle.width; height: window.dp(12)
                Repeater {
                    model: backend.volumeSteps
                    Rectangle {
                        required property int index
                        x: index*rail.width/backend.volumeSteps+window.dp(1)
                        width: Math.max(1,rail.width/backend.volumeSteps-window.dp(2)); height: rail.height
                        color: Math.round(volumeSlider.value*backend.volumeSteps) > (volumeSlider.mirrored ? backend.volumeSteps-index-1 : index) ? window.accent : window.hoverFill
                    }
                }
            }
            handle: Rectangle {
                x: volumeSlider.leftPadding+volumeSlider.visualPosition*(volumeSlider.availableWidth-width)
                y: (volumeSlider.height-height)/2
                implicitWidth: window.dp(9); implicitHeight: window.dp(22)
                color: window.ink; border.color: window.paper; border.width: 2
            }
            PadToolTip { visible: volumeSlider.hovered; delay: 600; text: volumeControl.microphone ? qsTr("Effect volume sent to others. Your voice volume stays unchanged.") : qsTr("Headphone effect volume") }
        }
        Label {
            objectName: volumeControl.microphone ? "microphoneVolumeLabel" : "headphoneVolumeLabel"
            text: window.volumeText(volumeControl.microphone ? backend.microphoneVolume : backend.headphoneVolume)
            color: window.ink; font: window.monoFont
            Layout.preferredWidth: window.percentageWidth; horizontalAlignment: Text.AlignRight
        }
    }
    component ColumnHeading: AbstractButton {
        id: heading
        property string column: ""
        property bool centered: false
        implicitHeight: window.headingHeight
        hoverEnabled: true
        contentItem: Text {
            text: heading.text.toLocaleUpperCase() + (window.sortColumn === heading.column ? (window.sortAscending ? " ↑" : " ↓") : "")
            color: window.mutedInk; font: window.monoFont
            leftPadding: heading.centered ? 0 : window.dp(12)
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: heading.centered ? Text.AlignHCenter : Text.AlignLeft
            elide: Text.ElideRight
        }
        background: Rectangle { color: heading.hovered ? window.hoverFill : window.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: window.rule } }
        onClicked: window.changeSort(column)
    }
    component PadDialog: Dialog {
        id: popup
        property bool closeInHeader: false
        font: window.font
        padding: window.dp(16); topPadding: window.dp(12)
        Material.roundedScale: theme.radius; Material.elevation: 0
        background: Rectangle { color: window.surface; border.color: window.accent; radius: theme.radius }
        header: Item {
            implicitHeight: popup.closeInHeader ? headerRow.implicitHeight+window.dp(24) : window.headingHeight+window.dp(12)
            RowLayout {
                id: headerRow
                anchors.fill: parent; anchors.leftMargin: window.dp(16); anchors.rightMargin: window.dp(16)
                anchors.topMargin: popup.closeInHeader ? window.dp(12) : 0
                anchors.bottomMargin: popup.closeInHeader ? window.dp(12) : 0
                Label { text: ">"; color: window.accent; font: window.monoFont }
                Label { text: popup.title.toLocaleUpperCase(); color: window.ink; font: window.monoBoldFont; Layout.fillWidth: true; elide: Text.ElideRight }
                KeyCap { visible: !popup.closeInHeader; label: "Esc" }
                PadButton {
                    objectName: "dialogHeaderCloseButton"
                    visible: popup.closeInHeader
                    Layout.alignment: Qt.AlignVCenter
                    padding: window.dp(12); topPadding: window.dp(8); bottomPadding: window.dp(8)
                    implicitHeight: Math.max(window.dp(40),textMetrics.height+window.dp(22))
                    text: qsTr("Close"); keyHint: "Esc"
                    onClicked: popup.reject()
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: window.rule }
        }
        footer: DialogButtonBox {
            visible: count > 0
            padding: window.dp(12)
            background: Rectangle { color: window.paper }
            delegate: PadButton {
                objectName: "dialogActionButton"
                text: popup.standardButtons === Dialog.Close ? qsTr("Close")
                    : DialogButtonBox.buttonRole === DialogButtonBox.AcceptRole ? qsTr("OK") : qsTr("Cancel")
            }
        }
        Overlay.modal: Rectangle { color: "#80000000" }
    }

    DropArea {
        id: fileDrop
        anchors.fill: parent
        onDropped: function(drop) { if (drop.hasUrls) backend.addUrls(drop.urls) }
    }
    ColumnLayout {
        id: contentLayout
        anchors.fill: parent; anchors.margins: window.dp(12); spacing: window.dp(10)
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true
        RowLayout {
            id: appHeader
            objectName: "appHeader"
            Layout.fillWidth: true; spacing: window.dp(10)
            PadButton { objectName: "addSoundButton"; text: qsTr("Add sound"); iconSymbol: "add"; keyHint: window.width >= window.dp(780) ? "Ctrl+O" : ""; onClicked: backend.chooseFiles() }
            Item { Layout.fillWidth: true }
            SmallButton { objectName: "preferencesButton"; symbol: "settings"; hint: qsTr("Preferences…"); onClicked: preferences.open() }
            SmallButton { objectName: "appMenuButton"; symbol: "menu"; hint: qsTr("More"); onClicked: appMenu.open() }
        }
        Rectangle {
            id: libraryPanel
            objectName: "libraryPanel"
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: window.headingHeight+window.rowHeight*3+window.dp(100)
            color: window.paper; border.color: fileDrop.containsDrag ? window.accent : window.rule; radius: theme.radius
            ColumnLayout {
                anchors.fill: parent; anchors.margins: window.dp(12); spacing: window.dp(8)
                RowLayout {
                    Layout.fillWidth: true; spacing: window.dp(12)
                    SectionTitle { number: "01"; caption: qsTr("Library"); Layout.fillWidth: true }
                    Label { text: qsTr("Sounds: %1").arg(backend.clips.length); color: window.mutedInk; font: window.monoFont }
                }
                Rectangle {
                    Layout.fillWidth: true; implicitHeight: window.headingHeight+window.dp(2)
                    color: window.surface; border.color: search.activeFocus ? window.accent : window.rule; radius: Math.min(3,theme.radius)
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: window.dp(10); anchors.rightMargin: window.dp(4); spacing: window.dp(8)
                        Text { text: ">_"; color: window.accent; font: window.monoFont }
                        TextField {
                            id: search; objectName: "searchField"
                            Layout.fillWidth: true; implicitHeight: window.headingHeight
                            placeholderText: qsTr("Search sounds…")
                            font: window.font; color: window.ink; placeholderTextColor: window.mutedInk
                            selectionColor: window.selection; selectedTextColor: window.selectionInk
                            padding: 0; selectByMouse: true
                            background: Item {}
                            onTextChanged: window.rebuildRows()
                        }
                        KeyCap { visible: !search.text.length && window.width >= window.dp(850); label: "Ctrl+F" }
                        SmallButton { symbol: "close"; implicitWidth: window.dp(28); implicitHeight: window.dp(28); hint: qsTr("Clear search"); visible: search.text.length > 0; onClicked: { search.clear(); library.forceActiveFocus() } }
                    }
                }
                Item {
                    id: tableArea
                    Layout.fillWidth: true; Layout.fillHeight: true
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
                        y: headings.height; width: parent.width; height: Math.max(0,parent.height-headings.height)
                        clip: true; model: soundRows; boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                            visible: library.contentHeight > library.height
                            implicitWidth: window.dp(5); padding: 0
                            contentItem: Rectangle { implicitWidth: window.dp(5); implicitHeight: window.dp(24); color: window.mutedInk; opacity: parent.pressed ? 1 : .5 }
                        }
                        delegate: Rectangle {
                            id: soundRow
                            required property int index
                            required property int originalIndex
                            required property string soundName
                            required property string fileName
                            required property string filePath
                            readonly property bool selected: window.selected === originalIndex
                            readonly property color rowInk: selected ? window.selectionInk : window.ink
                            readonly property color rowMutedInk: selected ? window.selectionInk : window.mutedInk
                            width: library.width; height: window.rowHeight
                            color: selected ? window.selection : rowMouse.containsMouse ? window.hoverFill : index%2 ? window.alternatePaper : window.paper
                            Rectangle { width: window.dp(3); height: parent.height; color: window.accent; visible: soundRow.selected }
                            Row {
                                anchors.fill: parent
                                Text { width: window.numberWidth; height: parent.height; text: (soundRow.originalIndex+1).toString().padStart(2,"0"); color: soundRow.rowMutedInk; font: window.monoFont; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                Item {
                                    width: window.nameWidth; height: parent.height
                                    PadIcon { x: window.dp(12); anchors.verticalCenter: parent.verticalCenter; width: window.dp(15); height: window.dp(15); symbol: backend.playing === soundRow.soundName ? "play" : "sound"; ink: soundRow.selected ? soundRow.rowInk : window.accent }
                                    Text { x: window.dp(38); width: parent.width-window.dp(46); height: parent.height; text: soundRow.soundName; color: soundRow.rowInk; font: window.styledFont(window.font,soundRow.selected,0); verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                }
                                Item {
                                    width: window.hotkeyWidth; height: parent.height
                                    KeyCap { x: window.dp(12); anchors.verticalCenter: parent.verticalCenter; label: soundRow.originalIndex < 9 ? (soundRow.originalIndex+1).toString() : "—"; labelColor: soundRow.rowMutedInk }
                                }
                                Text { width: window.fileWidth; height: parent.height; visible: window.fileColumnVisible; leftPadding: window.dp(12); rightPadding: window.dp(8); text: soundRow.fileName; color: soundRow.rowMutedInk; font: window.monoFont; verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle }
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
                        ColumnLayout {
                            anchors.centerIn: parent; width: Math.min(parent.width-window.dp(32),window.dp(460)); spacing: window.dp(12)
                            visible: soundRows.count === 0
                            Label { Layout.alignment: Qt.AlignHCenter; text: "[ " + (backend.clips.length ? "?" : "+") + " ]"; color: window.accent; font: window.monoBoldFont }
                            Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: backend.clips.length ? qsTr("No matching sounds") : qsTr("Your sound library starts here"); color: window.ink }
                            Label { Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap; text: backend.clips.length ? qsTr("Try another name or clear the search") : qsTr("Drop audio files here or press Ctrl+O"); color: window.mutedInk }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: window.rule }
                RowLayout {
                    Layout.fillWidth: true; spacing: window.dp(8)
                    PadIcon { symbol: "sound"; ink: window.mutedInk; Layout.preferredWidth: window.dp(14); Layout.preferredHeight: window.dp(14) }
                    Label { Layout.fillWidth: true; text: window.hasSelection ? backend.clips[window.selected].path.split('/').pop() : qsTr("No sound selected"); color: window.mutedInk; font: window.monoFont; elide: Text.ElideMiddle }
                    KeyCap { visible: window.width >= window.dp(760); label: "Enter" }
                    Label { visible: window.width >= window.dp(760); text: qsTr("Play sound"); color: window.mutedInk }
                    KeyCap { visible: window.width >= window.dp(900); label: "F2" }
                    Label { visible: window.width >= window.dp(900); text: qsTr("Rename"); color: window.mutedInk }
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: errorRow.implicitHeight+window.dp(14)
            visible: window.errorMessage.length > 0
            color: Qt.tint(window.surface,Qt.rgba(window.red.r,window.red.g,window.red.b,.10)); border.color: window.red; radius: Math.min(3,theme.radius)
            RowLayout {
                id: errorRow
                anchors.fill: parent; anchors.margins: window.dp(7); spacing: window.dp(10)
                Label { text: "!"; color: window.red; font: window.monoBoldFont; Layout.leftMargin: window.dp(5) }
                Label { Layout.fillWidth: true; text: window.errorMessage; color: window.ink; wrapMode: Text.WrapAnywhere; maximumLineCount: 2; elide: Text.ElideRight }
                SmallButton { symbol: "close"; implicitWidth: window.dp(28); implicitHeight: window.dp(28); hint: qsTr("Dismiss"); onClicked: { backend.clearError(); translator.clearError(); theme.clearError() } }
            }
        }
        Rectangle {
            objectName: "mixerPanel"
            Layout.fillWidth: true; implicitHeight: mixerContent.implicitHeight+window.dp(20)
            color: window.paper; border.color: window.rule; radius: theme.radius
            ColumnLayout {
                id: mixerContent
                anchors.fill: parent; anchors.margins: window.dp(10); spacing: window.dp(6)
                RowLayout {
                    Layout.fillWidth: true; spacing: window.dp(12)
                    SectionTitle { number: "02"; caption: qsTr("Levels"); Layout.fillWidth: true }
                    RouteDiagram { objectName: "routingDiagram" }
                }
                GridLayout {
                    id: volumeGrid; objectName: "volumeGrid"
                    Layout.fillWidth: true
                    columns: width < window.dp(850) ? 1 : 2
                    columnSpacing: window.dp(28); rowSpacing: window.dp(6)
                    VolumeControl { id: microphoneControl; microphone: true }
                    VolumeControl { id: headphoneControl; microphone: false }
                }
            }
        }
        Rectangle {
            objectName: "playerPanel"
            Layout.fillWidth: true; implicitHeight: transportContent.implicitHeight+window.dp(20)
            color: window.paper; border.color: window.rule; radius: theme.radius
            ColumnLayout {
                id: transportContent
                anchors.fill: parent; anchors.margins: window.dp(10); spacing: window.dp(8)
                RowLayout {
                    Layout.fillWidth: true; spacing: window.dp(7)
                    SmallButton { symbol: "previous"; hint: qsTr("Previous sound"); enabled: soundRows.count > 0; onClicked: window.moveSelection(-1,true) }
                    PadButton { objectName: "playButton"; iconSymbol: backend.playing && !backend.paused ? "pause" : "play"; primary: true; implicitWidth: window.dp(48); implicitHeight: window.dp(40); hint: (backend.playing ? (backend.paused ? qsTr("Resume") : qsTr("Pause")) : qsTr("Play selected sound")) + " (Space)"; enabled: (window.hasSelection || backend.playing.length > 0) && !backend.busy; onClicked: window.togglePlayback() }
                    SmallButton { objectName: "stopButton"; symbol: "stop"; hint: qsTr("Stop") + " (Esc)"; onClicked: backend.stop() }
                    SmallButton { symbol: "next"; hint: qsTr("Next sound"); enabled: soundRows.count > 0; onClicked: window.moveSelection(1,true) }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.minimumWidth: window.dp(60); Layout.leftMargin: window.dp(8); spacing: 0
                        Label { text: (backend.paused ? qsTr("Paused") : backend.playing ? qsTr("Now playing") : qsTr("Selected sound")).toLocaleUpperCase(); color: window.mutedInk; font: window.monoFont; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { objectName: "currentSoundLabel"; text: backend.playing || (window.hasSelection ? backend.clips[window.selected].name : qsTr("No sound selected")); color: window.ink; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    SmallButton { objectName: "repeatButton"; symbol: "repeat"; hint: qsTr("Repeat current sound"); checked: backend.repeat; onClicked: backend.repeat = !backend.repeat }
                    SmallButton { objectName: "microphoneOnlyButton"; symbol: "microphone"; hint: qsTr("Microphone only"); iconColor: window.red; checked: backend.outputMode === "microphone" && backend.playing.length > 0; enabled: window.hasSelection; onClicked: backend.playMicrophoneOnly(window.selected) }
                    SmallButton { objectName: "headphonesOnlyButton"; symbol: "headphones"; hint: qsTr("Headphones only"); iconColor: window.blue; checked: backend.outputMode === "headphones" && backend.playing.length > 0; enabled: window.hasSelection; onClicked: backend.preview(window.selected) }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: window.dp(10)
                    Label { text: "[03]"; color: window.accent; font: window.monoFont }
                    SmallButton { objectName: "seekBackButton"; symbol: "rewind"; hint: qsTr("Back 5 seconds"); implicitWidth: window.dp(28); implicitHeight: window.dp(28); enabled: backend.duration > 0 && !backend.busy; onClicked: backend.seekBy(-5) }
                    Slider {
                        id: playbackProgress; objectName: "playbackProgress"
                        Layout.fillWidth: true; Layout.minimumWidth: window.dp(90); implicitHeight: window.dp(28)
                        from: 0; to: 1; stepSize: backend.duration > 0 ? Math.min(1,5/backend.duration) : 1
                        enabled: backend.duration > 0 && !backend.busy
                        leftPadding: window.dp(4); rightPadding: window.dp(4)
                        Accessible.name: qsTr("Playback position")
                        onMoved: backend.seek(value)
                        Binding { target: playbackProgress; property: "value"; value: backend.progress; when: !playbackProgress.pressed; restoreMode: Binding.RestoreNone }
                        background: Rectangle {
                            x: playbackProgress.leftPadding+playbackProgress.handle.width/2
                            y: (playbackProgress.height-height)/2
                            width: playbackProgress.availableWidth-playbackProgress.handle.width; height: window.dp(8)
                            color: theme.colors.darker_background; border.color: window.rule
                            Rectangle {
                                width: backend.busy ? parent.width*.15 : playbackProgress.visualPosition*parent.width
                                height: parent.height; color: window.accent
                                SequentialAnimation on opacity {
                                    running: backend.busy; loops: Animation.Infinite
                                    NumberAnimation { from: .3; to: 1; duration: 600 }
                                    NumberAnimation { from: 1; to: .3; duration: 600 }
                                }
                                opacity: 1
                            }
                        }
                        handle: Rectangle {
                            x: playbackProgress.leftPadding+playbackProgress.visualPosition*(playbackProgress.availableWidth-width)
                            y: (playbackProgress.height-height)/2
                            implicitWidth: window.dp(8); implicitHeight: window.dp(18)
                            visible: playbackProgress.enabled
                            color: window.ink; border.color: window.paper; border.width: 2
                        }
                        PadToolTip { visible: playbackProgress.hovered && playbackProgress.enabled || playbackProgress.pressed; text: window.timeText(playbackProgress.value*backend.duration) }
                    }
                    SmallButton { objectName: "seekForwardButton"; symbol: "forward"; hint: qsTr("Forward 5 seconds"); implicitWidth: window.dp(28); implicitHeight: window.dp(28); enabled: backend.duration > 0 && !backend.busy; onClicked: backend.seekBy(5) }
                    Label { objectName: "playbackTimeLabel"; text: window.timeText(backend.position)+" / "+window.timeText(backend.duration); color: window.mutedInk; font: window.monoFont; Layout.preferredWidth: monoMetrics.advanceWidth(window.timeText(backend.duration)+" / "+window.timeText(backend.duration))+window.dp(4); horizontalAlignment: Text.AlignRight }
                }
            }
        }
    }
    PadMenu {
        id: appMenu; objectName: "appMenu"
        x: window.width-width-window.dp(12); y: appHeader.height+window.dp(20)
        PadMenuItem { text: qsTr("Add sound files…"); onTriggered: backend.chooseFiles() }
        PadMenuSeparator {}
        PadMenuItem { text: qsTr("Undo"); onTriggered: backend.undo() }
        PadMenuItem { text: qsTr("Redo"); onTriggered: backend.redo() }
        PadMenuItem { text: qsTr("Rename"); enabled: window.hasSelection; onTriggered: window.renameSelected() }
        PadMenuItem { text: qsTr("Remove from library"); enabled: window.hasSelection; onTriggered: backend.remove(window.selected) }
        PadMenuSeparator {}
        PadMenuItem { text: qsTr("File names"); checkable: true; checked: window.showFileNames; onTriggered: window.showFileNames = !window.showFileNames }
        PadMenuItem { text: qsTr("Preferences…"); onTriggered: preferences.open() }
        PadMenuItem { text: qsTr("Keyboard shortcuts"); onTriggered: help.open() }
        PadMenuSeparator {}
        PadMenuItem { text: qsTr("Language"); enabled: false }
        PadMenuItem { objectName: "englishLanguageItem"; text: "English"; checkable: true; checked: translator.language.split('-')[0] === "en"; onTriggered: translator.selectLanguage("en") }
        PadMenuItem { objectName: "russianLanguageItem"; text: "Русский"; checkable: true; checked: translator.language.split('-')[0] === "ru"; onTriggered: translator.selectLanguage("ru") }
        PadMenuSeparator {}
        PadMenuItem { text: qsTr("Exit"); onTriggered: Qt.quit() }
    }
    PadMenu {
        id: contextMenu; objectName: "soundContextMenu"
        PadMenuItem { text: qsTr("Play selected sound"); onTriggered: backend.play(window.selected) }
        PadMenuItem { text: qsTr("Microphone only"); onTriggered: backend.playMicrophoneOnly(window.selected) }
        PadMenuItem { text: qsTr("Headphones only"); onTriggered: backend.preview(window.selected) }
        PadMenuSeparator {}
        PadMenuItem { text: qsTr("Rename"); onTriggered: window.renameSelected() }
        PadMenuItem { text: qsTr("Remove from library"); onTriggered: backend.remove(window.selected) }
    }
    PadDialog {
        id: preferences; objectName: "preferencesDialog"
        closeInHeader: true
        footer: null
        anchors.centerIn: parent; width: Math.min(window.width-window.dp(40),window.dp(640))
        height: Math.min(window.height-window.dp(24),implicitHeight)
        title: qsTr("Preferences")
        modal: true; standardButtons: Dialog.NoButton
        contentItem: ScrollView {
            id: preferencesScroll
            implicitHeight: preferencesFields.implicitHeight
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
            id: preferencesFields
            width: preferencesScroll.availableWidth
            spacing: window.dp(12)
            Label { text: qsTr("Appearance"); color: window.mutedInk; font: window.monoFont }
            PadCheckBox {
                objectName: "omarchySyncCheckBox"
                Layout.fillWidth: true
                text: qsTr("Sync colors with Omarchy")
                checked: theme.syncOmarchy
                onToggled: if (checked !== theme.syncOmarchy) theme.syncOmarchy = checked
            }
            Label {
                Layout.fillWidth: true
                visible: theme.syncOmarchy && !theme.omarchyAvailable
                text: qsTr("Omarchy theme not found. Using the built-in theme below.")
                color: window.mutedInk; wrapMode: Text.WordWrap
            }
            PadComboBox {
                objectName: "themeComboBox"
                Layout.fillWidth: true
                enabled: !theme.syncOmarchy
                opacity: enabled ? 1 : .55
                model: theme.presets; textRole: "name"; valueRole: "id"
                currentIndex: window.modelIndex(model,"id",theme.preset)
                onActivated: theme.selectPreset(currentValue)
                Accessible.name: qsTr("Built-in theme")
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: window.rule }
            Label { text: qsTr("Microphone"); color: window.mutedInk; font: window.monoFont }
            PadComboBox {
                objectName: "microphoneComboBox"
                Layout.fillWidth: true
                model: backend.microphones; textRole: "label"; valueRole: "name"
                currentIndex: window.modelIndex(model,"name",backend.microphone)
                displayText: currentIndex < 0 ? qsTr("Select physical microphone") : currentText
                onActivated: backend.microphone = currentValue
            }
            Label { text: qsTr("Send sounds to"); color: window.mutedInk; font: window.monoFont }
            PadComboBox {
                Layout.fillWidth: true
                model: backend.applications; textRole: "label"; valueRole: "serial"
                currentIndex: window.modelIndex(model,"serial",backend.target)
                displayText: currentIndex < 0 ? qsTr("Target app disconnected") : currentText
                onActivated: backend.target = currentValue
            }
            Label { Layout.fillWidth: true; text: backend.status; color: window.mutedInk; wrapMode: Text.WordWrap }
            PadButton { text: qsTr("Keyboard shortcuts…"); onClicked: { preferences.close(); help.open() } }
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
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Enter or double-click starts the selected sound. Space pauses/resumes, Esc stops, ↑/↓ selects a sound, ←/→ seeks 5 seconds. Drag the position bar to seek. Ctrl+F searches, Ctrl+O adds files, F2 renames, Delete removes from the list, Ctrl+Z undoes changes.") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Keys 1–9 play sounds by their original list number, even after sorting. Global keys Ctrl+Alt+1–9 work in other apps after enabling them; Ctrl+Alt+0 stops playback.") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: qsTr("Ctrl+Shift+Z redoes changes. Q quits. ? opens this help.") }
            PadButton { text: qsTr("Enable global shortcuts"); onClicked: { backend.enableHotkeys(); help.close() } }
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
        contentItem: TextField {
            id: renameField; objectName: "renameField"
            selectByMouse: true; color: window.ink; selectionColor: window.selection; selectedTextColor: window.selectionInk
            padding: window.dp(10)
            background: Rectangle { color: window.paper; border.color: renameField.activeFocus ? window.accent : window.rule; radius: Math.min(3,theme.radius) }
            onAccepted: renameDialog.accept()
        }
        onAccepted: backend.rename(clipIndex,renameField.text)
        onClosed: library.forceActiveFocus()
    }
}
