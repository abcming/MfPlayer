pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick

Image {
    id: root

    property string embyUrl: ""
    property bool lazyLoad: false

    asynchronous: true
    cache: false
    fillMode: Image.PreserveAspectFit
    mipmap: true
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
            // Fast path: if the image is already cached locally, load it
            // immediately — the 80ms lazyTimer is only needed for cold
            // (HTTP download) paths to prevent a download storm during
            // fast scroll. Without this check, every delegate shows blank
            // for 80ms on every scroll pass, even for cached images.
            // 一次调用同时当命中判据和结果用: providerUrl() 未命中返回空。
            // 别拆回 cachedImageUrl() + _providerUrl() 两次 —— 快速滚动时每个
            // delegate 都走这条, 两次 Q_INVOKABLE 反射 + 两次 MD5 会卡住主线程
            let pu = _providerUrl(url)
            if (pu) {
                source = pu
                _busy = false
                return
            }
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
        // 路径由 C++ 侧解析并编码进 URL —— provider 跑在 reader 线程, 不能让它
        // 回头读 m_imageCache。编码也必须留在 C++: JS 的 btoa 遇到非 ASCII 直接抛
        return Server.cache.providerUrl(url)
    }

    function _startLoad() {
        let url = embyUrl
        if (!url) return
        // 同上 —— 一次调用兼作命中判据, 不要拆成两次
        let pu = _providerUrl(url)
        if (pu) {
            // Cache hit — use provider URL so sourceSize is respected
            source = pu
            _busy = false
            return
        }
        // Cache miss — start download. imageReady will queue texture creation.
        _pendingEmbyUrl = url
        Server.cache.fetchImage(url)
    }

    // Lazy-load timer with random stagger (100-200ms) to spread image-load
    // callbacks across time when many delegates enter the viewport at once.
    // Without jitter, fast scrolling creates 20+ Timers that all fire at once,
    // sending a flood of QML→C++ cache lookups + ImageProvider requests.
    Timer {
        id: lazyTimer
        interval: 100 + Math.floor(Math.random() * 100)
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
