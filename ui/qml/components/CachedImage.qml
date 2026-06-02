import QtQuick

Image {
    id: root

    property string embyUrl: ""
    property bool lazyLoad: false

    asynchronous: true
    cache: false
    fillMode: Image.PreserveAspectFit
    mipmap: false
    smooth: true

    // Queue grant callback — called by ImageLoadQueue when a slot is available.
    // Only used for fetch (download) paths. Cache hits bypass the queue.
    function grantLoad(url) {
        _queued = true
        _queuedUrl = url
        if (_readyToDisplay) {
            source = _queuedUrl
            _busy = false
        }
    }

    property bool _busy: false
    property string _lastEmbyUrl: ""
    property string _pendingEmbyUrl: ""   // embyUrl we're waiting for download
    property bool _readyToDisplay: false   // true when imageReady provided a file:// URL
    property bool _queued: false           // true when queue granted permission to load
    property string _queuedUrl: ""
    property bool _waitingForQueue: false  // true if registered with queue, waiting for grant

    visible: status === Image.Ready

    onStatusChanged: {
        if (status === Image.Ready || status === Image.Error) {
            if (_waitingForQueue) {
                ImageLoadQueue.release(root)
                _waitingForQueue = false
            }
        }
    }

    onEmbyUrlChanged: {
        let url = embyUrl
        if (url !== _lastEmbyUrl) {
            _busy = true
            _lastEmbyUrl = url
            _resetLoad()
            source = ""
        }
        if (!url) {
            source = ""
            _busy = false
            _lastEmbyUrl = ""
            return
        }
        if (lazyLoad) {
            lazyTimer.restart()
            return
        }
        _startLoad()
    }

    function _resetLoad() {
        if (_waitingForQueue) {
            ImageLoadQueue.cancel(root)
            _waitingForQueue = false
        }
        _readyToDisplay = false
        _queued = false
        _queuedUrl = ""
        _pendingEmbyUrl = ""
    }

    function _providerUrl(url) {
        return "image://imgcache/" + Qt.md5(url)
    }

    function _startLoad() {
        let url = embyUrl
        if (!url) return
        let cached = Server.cache.cachedImageUrl(url)
        if (cached) {
            // Cache hit — use provider URL so sourceSize is respected
            source = _providerUrl(url)
            _busy = false
            return
        }
        // Cache miss — start download. imageReady will queue texture creation.
        _pendingEmbyUrl = url
        Server.cache.fetchImage(url)
    }

    Timer {
        id: lazyTimer
        interval: 80
        onTriggered: _startLoad()
    }

    Connections {
        target: Server.cache
        enabled: root._pendingEmbyUrl !== ""

        function onImageReady(url, localPath) {
            // Only handle if this is for the URL we're waiting for
            if (url !== root._pendingEmbyUrl) return
            root._pendingEmbyUrl = ""

            if (!localPath) {
                root._busy = false
                return
            }

            // Register with load queue — use provider URL so sourceSize is respected
            root._readyToDisplay = true
            root._waitingForQueue = true
            ImageLoadQueue.request(root, root._providerUrl(url))
        }
    }

    Component.onDestruction: {
        if (_waitingForQueue) {
            ImageLoadQueue.cancel(root)
        }
    }
}
