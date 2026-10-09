import QtQuick 2.9
import QtQuick.Controls 2.2

import ComputerManager 1.0
import Session 1.0
import SystemProperties 1.0
import SdlGamepadKeyNavigation 1.0

// PROTOTYPE (Arium): black with the Arium icon from the press of Play until the stream's
// own window is up, and again between the game ending and the page returning. Throwaway.
Item {
    id: segue

    property Session session
    property string problem: ""

    function finish() {
        // Left with the middle button: the page goes to its screen for the game still running,
        // and is given a moment to get there behind this black screen before it shows.
        if (arium.takeLeftByMiddleButton() && !problem) {
            arium.note("left with the middle button; the page is sent to the running game's screen")
            // (This screen is made by AriumWeb.qml, where "ariumWeb" is that page itself; from
            // main.qml's side the same name is the Loader that holds it.)
            var page = ariumWeb.item ? ariumWeb.item : ariumWeb
            page.go("/playing")
            leaveTimer.start()
            return
        }
        arium.note("back to the page" + (problem ? " after: " + problem : ""))
        stackView.pop(StackView.Immediate)
    }

    Timer { id: leaveTimer; interval: 350; onTriggered: stackView.pop(StackView.Immediate) }

    function begin() {
        session = arium.newSession(ComputerManager)
        if (!session) {
            problem = arium.problem()
            arium.note("no session: " + problem)
            problemTimer.start()
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
            problem = problem || "The stream could not be set up"
            problemTimer.start()
            return
        }
        startTimer.start()
    }

    StackView.onActivated: {
        // One frame of black first, so the page is never the last thing seen
        executeTimer.start()
    }

    Timer { id: executeTimer; interval: 50; onTriggered: begin() }
    Timer { id: startTimer; interval: 0; onTriggered: { gc(); arium.armTestLeave(); session.start() } }
    Timer { id: problemTimer; interval: 6000; onTriggered: finish() }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    // Same size and place as the icon on the gaming PC's curtain (AriumPlay.cs), so the
    // stream's first picture lands on top of this one
    Image {
        width: Math.round(parent.height / 4)
        height: width
        x: Math.round((parent.width - width) / 2)
        y: Math.round(parent.height / 2 - height)
        source: "qrc:/res/arium.png"
        smooth: true
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
