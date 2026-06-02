import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Debug overlay showing video/audio/cache stats.
// Press I to toggle visibility in PlayerPage.

Rectangle {
    id: root

    property bool active: false

    anchors {
        top: parent ? parent.top : undefined
        left: parent ? parent.left : undefined
        margins: 16
    }
    z: 100
    width: 380
    height: 320
    radius: 6
    color: Qt.rgba(0, 0, 0, 0.80)
    visible: root.active

    Timer {
        id: debugTimer
        interval: 200
        running: root.visible
        repeat: true
        onTriggered: debugText.text = buildDebugText()
    }

    Text {
        id: debugText
        anchors { fill: parent; margins: 10 }
        color: Theme.textTertiary
        font.pixelSize: 11
    }

    function fs(b) {
        if (!b || b < 0) return "? bps"
        var kb = b / 1000, mb = kb / 1000
        return mb >= 1 ? mb.toFixed(2) + " Mbps" : kb.toFixed(0) + " kbps"
    }

    function fc(b) {
        if (!b || b <= 0) return "?"
        var kb = b / 1000, mb = kb / 1000, gb = mb / 1000
        return gb >= 1 ? gb.toFixed(1) + " GB" : mb >= 1 ? mb.toFixed(0) + " MB" : kb.toFixed(0) + " KB"
    }

    function buildDebugText() {
        var v = Playback.mpv.videoOutParams || {}
        var vi = Playback.mpv.videoParams || {}
        var a = Playback.mpv.audioOutParams || {}
        var s = Playback.mpv.stats || {}
        var L = []

        // Video
        var w = v.w || vi.w || "?", h = v.h || vi.h || "?"
        var dw = v.dw || vi.dw || w, dh = v.dh || vi.dh || h
        var pix = v.pixelformat || vi.pixelformat || "?"
        var hwp = v["hw-pixelformat"]
        var fmts = hwp ? pix + " (" + hwp + ")" : pix
        var bpp = (v["average-bpp"] !== undefined) ? "  " + v["average-bpp"] + " bpp" : ""

        L.push('Video:')
        L.push("  " + w + "x" + h + " -> " + dw + "x" + dh)
        L.push("  " + fmts + bpp)

        var vc = s.videoCodec, vbr = s.videoBitrate
        var vi2 = vc || ""
        if (vbr > 0) vi2 += "  " + fs(vbr)
        if (vi2) L.push("  " + vi2)

        var hw = s.hwdec
        if (hw) L.push("  hwdec: " + hw)

        var tr = v.colortransfer || s.srcColorTransfer || vi.colortransfer
        var pr = v.colorprim || s.srcColorPrim || vi.colorprim
        var mx = v.colormatrix || s.srcColorMatrix || vi.colormatrix
        if (mx === "dolbyvision") {
            if (!pr || pr === "?") pr = "bt.2020"
            if (!tr || tr === "?") tr = "pq"
        }
        tr = tr || "?"; pr = pr || "?"; mx = mx || "?"
        var lv = v.colorlevels || vi.colorlevels || "?"
        L.push("  " + tr + " / " + pr + " / " + mx + "  levels=" + lv)

        if (tr === "pq") L.push("  [HDR] HDR10")
        else if (tr === "hlg") L.push("  [HDR] HLG")
        else if (tr === "dolbyvision") L.push("  [HDR] Dolby Vision")

        // FPS
        var cf = s.containerFps, ef = s.estimatedFps, df = s.displayFps
        if (cf || ef || df) {
            L.push("")
            L.push("FPS:")
            if (cf) L.push("  container: " + cf.toFixed(3))
            if (ef) L.push("  estimated: " + ef.toFixed(2))
            if (df) L.push("  display:   " + df.toFixed(3))
            var dr = s.frameDrops, dy = s.frameDelayed
            if (dr !== undefined || dy !== undefined)
                L.push("  dropped: " + (dr||0) + "  delayed: " + (dy||0))
            var av = s.avsync
            if (av !== undefined)
                L.push("  A/V: " + (av * 1000).toFixed(2) + " ms")
        }

        // Audio
        var ach = a.channels, ar = a.samplerate, af = a.format
        var ac = s.audioCodec, abr = s.audioBitrate
        if (ac || af || ach || ar) {
            L.push("")
            L.push("Audio:")
            var ai2 = ac || ""
            if (af) ai2 += " / " + af
            L.push("  " + ai2)
            L.push("  " + (ach||"?") + "ch  " + (ar ? (ar/1000).toFixed(0)+" kHz" : "?"))
            if (abr > 0) L.push("  " + fs(abr))
        }

        // Cache
        var cs = s.cacheSpeed, cu = s.cacheUsed, ct = s.cacheTotal, cd = s.cacheDuration
        if (cs || cu || ct || cd) {
            L.push("")
            L.push("Cache:")
            if (cu !== undefined && ct > 0)
                L.push("  " + fc(cu) + " / " + fc(ct) + " (" + (cu/ct*100).toFixed(0) + "%)")
            if (cs > 0) L.push("  " + fs(cs) + "/s")
            if (cd > 0) L.push("  buffer: " + cd.toFixed(1) + "s")
        }

        return L.join("\n")
    }
}
