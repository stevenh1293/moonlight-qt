import QtQuick 2.9
import QtQuick.Controls 2.2

import SdlGamepadKeyNavigation 1.0

// PROTOTYPE (Arium): what the StackView holds while Arium's page is showing. The page itself
// is AriumWeb.qml, placed over this by main.qml. Throwaway.
Item {
    objectName: "Arium"

    StackView.onActivated: {
        // The app reads the controller and hands each press to the page (see AriumWeb.qml)
        SdlGamepadKeyNavigation.setAriumMode(true)
        SdlGamepadKeyNavigation.enable()
        arium.note("controllers the app has open: " + SdlGamepadKeyNavigation.describeGamepads())
        arium.note("page shown")
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }
}
