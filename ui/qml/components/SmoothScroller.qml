pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick

// Physics-based smooth scroller: velocity accumulation + damping.
//
//  Wheel → velocity += delta       (each event stacks, no restart)
//  Timer → contentY += velocity*dt  (frame-rate aware, damping each frame)
//
// Replaces WheelHandler + NumberAnimation combos in Flickable/ListView/GridView.
// The parent Flickable must have interactive: false.
Item {
    id: root

    required property Flickable target

    property real velocity: 0
    property real wheelStep: 100      // px per wheel notch (angleDelta / 120 * wheelStep)
    property real damping: 0.92      // velocity retained per frame
    property real stopThreshold: 1.0 // velocity below this stops the ticker

    // Keep us pinned to the Flickable viewport (content coordinates shift with scroll)
    y: target.contentY
    width: target.width
    height: target.height

    WheelHandler {
        onWheel: (event) => {
            event.accepted = true
            root.velocity -= event.angleDelta.y / 120 * root.wheelStep
            root.velocity -= event.angleDelta.x / 120 * root.wheelStep * 0.5
            if (!ticker.running) {
                ticker.lastTime = Date.now()
                ticker.running = true
            }
        }
    }

    Timer {
        id: ticker
        interval: 1   // fire ASAP — Qt batches to roughly display rate
        repeat: true
        running: false

        property real lastTime: 0

        onTriggered: {
            let now = Date.now()
            let dt = (now - lastTime) / 1000.0
            lastTime = now

            if (dt <= 0 || dt > 0.1) return

            let maxScroll = Math.max(0, root.target.contentHeight - root.target.height)
            let targetY = root.target.contentY + root.velocity * dt

            if (targetY <= 0) {
                root.target.contentY = 0
                root.velocity = 0
                running = false
                return
            }
            if (targetY >= maxScroll) {
                root.target.contentY = maxScroll
                root.velocity = 0
                running = false
                return
            }

            root.target.contentY = targetY
            root.velocity *= root.damping

            if (Math.abs(root.velocity) < root.stopThreshold) {
                root.target.contentY = Math.round(root.target.contentY)
                root.velocity = 0
                running = false
            }
        }
    }
}
