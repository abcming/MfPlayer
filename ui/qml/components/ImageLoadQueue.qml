pragma Singleton
import QtQuick

// Global image load queue — limits concurrent texture creation to prevent
// render thread overload when many CachedImage elements load simultaneously.
// Without this, 15-20 images loading at once → 90ms of texture uploads → 5 dropped frames.
QtObject {
    id: root

    readonly property int maxConcurrent: 3
    property int active: 0
    property var entries: []  // [{caller, url}]

    // Register a caller to load url. Grants immediately if slots available,
    // otherwise queues and grants in order when a slot frees up.
    function request(caller, url) {
        // Already queued — just update URL (e.g. imageReady provided the file:// path)
        for (let i = 0; i < entries.length; i++) {
            if (entries[i].caller === caller) {
                let e = entries.slice();  // copy-on-write for binding
                e[i] = {caller: caller, url: url};
                entries = e;
                _tryGrant();
                return;
            }
        }

        // Slot available — grant immediately
        if (active < maxConcurrent) {
            active++;
            caller.grantLoad(url);
            return;
        }

        // Queue it
        let e = entries.slice();
        e.push({caller: caller, url: url});
        entries = e;
    }

    // Caller done loading (success or error) — release slot
    function release(caller) {
        // Remove from queue if still waiting
        let filtered = entries.filter(e => e.caller !== caller);
        if (filtered.length !== entries.length) {
            entries = filtered;
        }
        active = Math.max(0, active - 1);
        _tryGrant();
    }

    // Cancel a pending request (e.g. URL changed or caller destroyed)
    function cancel(caller) {
        let filtered = entries.filter(e => e.caller !== caller);
        if (filtered.length !== entries.length) {
            entries = filtered;
        }
        // Don't decrement active — caller wasn't granted yet
    }

    function _tryGrant() {
        while (entries.length > 0 && active < maxConcurrent) {
            let next = entries[0];
            entries = entries.slice(1);
            active++;
            next.caller.grantLoad(next.url);
        }
    }
}
