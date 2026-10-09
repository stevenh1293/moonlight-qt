import QtQuick 2.9
import QtQuick.Controls 2.2

import SdlGamepadKeyNavigation 1.0

// PROTOTYPE (Arium): what the StackView holds while Arium's page is showing. The page itself
// is AriumWeb.qml, placed over this by main.qml. Throwaway.
Item {
    objectName: "Arium"

    StackView.onActivated: {
        // The page reads the controller itself; Moonlight's own menu navigation would turn B into Escape
        SdlGamepadKeyNavigation.disable()
        arium.note("page shown")
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
    }
}
