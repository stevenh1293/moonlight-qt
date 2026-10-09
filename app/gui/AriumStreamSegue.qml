import QtQuick 2.9
import QtQuick.Controls 2.2

import ComputerManager 1.0
import Session 1.0
import SystemProperties 1.0

// PROTOTYPE (Arium): black with the Arium icon from the press of Play until the stream's
// own window is up, and again between the game ending and the page returning. Throwaway.
Item {
    id: segue

    property Session session
    property string problem: ""

    function finish() {
        arium.note("back to the page" + (problem ? " after: " + problem : ""))
        stackView.pop(StackView.Immediate)
    }

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
    Timer { id: startTimer; interval: 0; onTriggered: { gc(); session.start() } }
    Timer { id: problemTimer; interval: 6000; onTriggered: finish() }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }

    Image {
        anchors.centerIn: parent
        source: "qrc:/res/arium.png"
        width: 192
        height: 192
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
