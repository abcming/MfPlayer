pragma Singleton
import QtQuick
import QtQuick.Controls

// Centralized navigation actions. Eliminates implicit scope chain coupling
// where child components accessed pageStack/detailPage/playerPage from
// Main.qml's scope without declaring the dependency.
//
// Main.qml sets pageStack and the Component references in onCompleted.
// All other components call Nav.pushDetail(itemId) etc. instead of
// reaching up through the QML scope chain.
QtObject {
    property StackView pageStack: null
    property Component detailPage: null
    property Component playerPage: null
    property Component browsePage: null
    property var loginDialog: null

    readonly property int depth: pageStack ? pageStack.depth : 0

    function pushDetail(itemId) {
        if (pageStack && detailPage)
            pageStack.push(detailPage, {itemId: itemId})
    }

    function pushPlayer(props, immediate) {
        if (pageStack && playerPage) {
            var op = immediate ? StackView.Immediate : StackView.Transition
            pageStack.push(playerPage, props || {}, op)
        }
    }

    function pop() {
        if (pageStack) pageStack.pop()
    }

    function popToRoot() {
        if (pageStack) while (pageStack.depth > 1) pageStack.pop()
    }

    function openLogin() {
        if (loginDialog) loginDialog.open()
    }
}
