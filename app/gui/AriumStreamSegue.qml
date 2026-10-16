import QtQuick 2.9
import QtQuick.Controls 2.2

import ComputerManager 1.0
import Session 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

// Arium's picture on black from the press of Play until the stream's own window is up, and again
// between the game ending and the page returning. Before the stream, the gaming PC is found (and
// added, the first time) and this PC paired with it if it is not yet, with nothing asked of anyone.
Item {
    id: segue

    property Session session
    property string problem: ""
    // Where the gaming PC is, as the page had it from Arium
    property string gamingPc: ""
    // Gone back to the page: anything the bridge says after that is not for this screen
    property bool done: false
    // A stream was started (the session object itself is let go of once it has finished)
    property bool streamed: false

    // (This screen is made by AriumWeb.qml, where "ariumWeb" is that page itself; from main.qml's
    // side the same name is the Loader that holds it.)
    readonly property var page: ariumWeb.item ? ariumWeb.item : ariumWeb

    function finish() {
        if (done) return
        done = true
        // Left with the middle button: the page goes to its screen for the game still running,
        // and is given a moment to get there behind this black screen before it shows.
        if (arium.takeLeftByMiddleButton() && !problem) {
            arium.note("left with the middle button; the page is sent to the running game's screen")
            page.go("/playing")
            leaveTimer.start()
            return
        }
        arium.note("back to the page" + (problem ? " after: " + problem : ""))
        // A stream that ended (not one that never began): the page shows the game it was for, if
        // the game did not end with it
        if (streamed) page.returned()
        stackView.pop(StackView.Immediate)
    }

    Timer { id: leaveTimer; interval: 350; onTriggered: stackView.pop(StackView.Immediate) }

    function fail(text) {
        if (problem || done) return
        problem = text
        arium.note("cannot stream: " + text)
        problemTimer.start()
    }

    Connections {
        target: arium
        enabled: !segue.done
        function onReady() { stream() }
        function onFailed(text) { fail(text) }
        function onPinToPass(pin, name) {
            page.passPin(pin, name, function(told) {
                // The PIN never reached the gaming PC: say why rather than wait out the pairing
                if (told !== "sent") fail(told || "This PC couldn't be set up for games.")
            })
        }
    }

    function begin() {
        preparing.start()
        arium.prepare(ComputerManager, gamingPc)
    }

    // Finding the gaming PC and pairing with it are given this long: a pairing the gaming PC never
    // answers would otherwise leave this screen up for good.
    Timer {
        id: preparing
        interval: 45000
        onTriggered: fail("This PC couldn't be set up for games. Try again; if it happens again, restart the Arium app.")
    }

    // A way out of this screen by keyboard as well: Escape goes back to the page, Ctrl+Q closes the app
    focus: true
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Q && (event.modifiers & Qt.ControlModifier)) Qt.quit()
        else if (event.key === Qt.Key_Escape && !session) { problem = problem || " "; finish() }
    }

    function stream() {
        preparing.stop()
        if (problem) return
        session = arium.newSession(ComputerManager, gamingPc, window)
        if (!session) {
            fail(arium.problem())
            return
        }
        arium.note("session created")
        session.stageFailed.connect(function(stage, errorCode, failingPorts) {
            problem = "Starting " + stage + " failed: error " + errorCode
        })
        session.displayLaunchError.connect(function(text) { problem = text })
        session.connectionStarted.connect(function() { arium.note("connection started") })
        session.sessionFinished.connect(function(portTestResult) {
            arium.note("session finished")
            if (problem) {
                problemTimer.start()
            }
            else {
                finish()
            }
        })
        session.readyForDeletion.connect(function() { session = null; gc() })

        // The stream reads the controller itself; the app's reading must be out of its way
        SdlGamepadKeyNavigation.disable()
        SystemProperties.waitForAsyncLoad()
        if (!session.initialize(window)) {
            fail(problem || "The stream could not be set up.")
            return
        }
        startTimer.start()
    }

    StackView.onActivated: {
        // One frame of black first, so the page is never the last thing seen
        executeTimer.start()
    }

    Timer { id: executeTimer; interval: 50; onTriggered: begin() }
    Timer { id: startTimer; interval: 0; onTriggered: { gc(); arium.armTestLeave(); streamed = true; session.start() } }
    Timer { id: problemTimer; interval: 6000; onTriggered: finish() }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    // Arium's picture over the whole screen, fitted the same way as on the gaming PC's curtain
    // (AriumPlay.cs), so the stream's first picture lands on top of this one
    Image {
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        source: "qrc:/res/arium_background.png"
        smooth: true
        mipmap: true
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 80
        visible: problem !== ""
        text: problem
        color: "#bbbbbb"
        font.pointSize: 16
        wrapMode: Text.Wrap
    }
}
