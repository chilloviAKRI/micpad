import QtQuick

Item {
    id: icon
    property string symbol: "play"
    property color ink: "#3c3c3c"
    implicitWidth: 18
    implicitHeight: 18
    onSymbolChanged: drawing.requestPaint()
    onInkChanged: drawing.requestPaint()
    Canvas {
        id: drawing
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            let c = getContext("2d")
            c.reset()
            c.scale(width/20,height/20)
            c.strokeStyle = icon.ink; c.fillStyle = icon.ink
            c.lineWidth = 2; c.lineCap = "square"; c.lineJoin = "miter"
            function line(x1,y1,x2,y2) { c.beginPath(); c.moveTo(x1,y1); c.lineTo(x2,y2); c.stroke() }
            function triangle(x,y,reverse) {
                c.beginPath(); c.moveTo(x,y); c.lineTo(x+(reverse ? -9 : 9),y+6)
                c.lineTo(x,y+12); c.closePath(); c.fill()
            }
            switch (icon.symbol) {
            case "play": triangle(6,4,false); break
            case "pause": c.fillRect(5,4,4,12); c.fillRect(11,4,4,12); break
            case "rewind": triangle(10,4,true); triangle(18,4,true); break
            case "forward": triangle(2,4,false); triangle(10,4,false); break
            case "stop": c.fillRect(5,5,10,10); break
            case "previous": triangle(16,4,true); line(5,4,5,16); break
            case "next": triangle(4,4,false); line(15,4,15,16); break
            case "microphone":
                c.fillRect(7,2,6,10)
                c.beginPath(); c.moveTo(4,8); c.lineTo(4,15); c.lineTo(16,15); c.lineTo(16,8); c.stroke()
                line(10,15,10,19); line(7,19,13,19); break
            case "headphones":
                c.beginPath(); c.moveTo(3,15); c.lineTo(3,6); c.lineTo(6,3); c.lineTo(14,3); c.lineTo(17,6); c.lineTo(17,15); c.stroke()
                c.fillRect(2,10,4,8); c.fillRect(14,10,4,8); break
            case "add":
                c.beginPath(); c.moveTo(2,5); c.lineTo(8,5); c.lineTo(10,7); c.lineTo(18,7)
                c.lineTo(18,17); c.lineTo(2,17); c.closePath(); c.stroke()
                line(10,10,10,15); line(7.5,12.5,12.5,12.5); break
            case "repeat":
                c.beginPath(); c.moveTo(3,8); c.lineTo(3,5); c.lineTo(16,5); c.lineTo(13,2); c.stroke()
                c.beginPath(); c.moveTo(17,12); c.lineTo(17,15); c.lineTo(4,15); c.lineTo(7,18); c.stroke(); break
            case "volume":
                c.beginPath(); c.moveTo(2,8); c.lineTo(6,8); c.lineTo(10,4); c.lineTo(10,16)
                c.lineTo(6,12); c.lineTo(2,12); c.closePath(); c.fill()
                c.beginPath(); c.arc(10,10,5,-.8,.8); c.stroke()
                c.beginPath(); c.arc(10,10,8,-.8,.8); c.stroke(); break
            case "search": c.strokeRect(3,3,9,9); line(12,12,17,17); break
            case "close": line(5,5,15,15); line(15,5,5,15); break
            case "settings":
                c.strokeRect(5,5,10,10); c.fillRect(8,8,4,4)
                line(8,2,8,5); line(12,2,12,5); line(8,15,8,18); line(12,15,12,18)
                line(2,8,5,8); line(2,12,5,12); line(15,8,18,8); line(15,12,18,12); break
            case "sound":
                line(6,4,6,14); line(6,4,15,2); line(15,2,15,12)
                c.fillRect(2,12,5,4); c.fillRect(11,10,5,4); break
            case "terminal": line(3,4,9,10); line(9,10,3,16); line(12,16,18,16); break
            case "menu": line(3,5,17,5); line(3,10,17,10); line(3,15,17,15); break
            case "check": line(4,10,8,14); line(8,14,16,5); break
            case "chevron": line(5,7,10,12); line(10,12,15,7); break
            }
        }
    }
}
