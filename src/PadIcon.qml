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
            c.lineWidth = 1.6; c.lineCap = "round"; c.lineJoin = "round"
            function line(x1,y1,x2,y2) { c.beginPath(); c.moveTo(x1,y1); c.lineTo(x2,y2); c.stroke() }
            function triangle(x,y,reverse) {
                c.beginPath(); c.moveTo(x,y); c.lineTo(x+(reverse ? -9 : 9),y+6)
                c.lineTo(x,y+12); c.closePath(); c.fill()
            }
            switch (icon.symbol) {
            case "play": triangle(6,4,false); break
            case "stop": c.fillRect(5,5,11,11); break
            case "previous": triangle(15,4,true); line(4,4,4,16); break
            case "next": triangle(5,4,false); line(16,4,16,16); break
            case "microphone":
                c.beginPath(); c.roundedRect(7,2,6,11,3,3); c.fill()
                c.beginPath(); c.arc(10,10,6,0,Math.PI); c.stroke()
                line(4,8,4,10); line(16,8,16,10); line(10,16,10,19); line(7,19,13,19); break
            case "headphones":
                c.beginPath(); c.arc(10,10,7,Math.PI,2*Math.PI); c.stroke()
                c.fillRect(2,9,4,8); c.fillRect(14,9,4,8); break
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
            case "search": c.beginPath(); c.arc(8,8,5,0,2*Math.PI); c.stroke(); line(12,12,17,17); break
            case "close": line(5,5,15,15); line(15,5,5,15); break
            case "settings":
                c.beginPath(); c.arc(10,10,5.8,0,2*Math.PI); c.stroke()
                c.beginPath(); c.arc(10,10,2,0,2*Math.PI); c.stroke()
                for (let i=0; i<8; ++i) { let a=i*Math.PI/4; line(10+6*Math.cos(a),10+6*Math.sin(a),10+8*Math.cos(a),10+8*Math.sin(a)) }
                break
            case "sound":
                line(6,4,6,14); line(6,4,15,2); line(15,2,15,12)
                c.beginPath(); c.ellipse(2,12,5,4); c.fill()
                c.beginPath(); c.ellipse(11,10,5,4); c.fill(); break
            }
        }
    }
}
