import QtQuick 2.9
import QtQuick.Controls 2.2
import QtWebView

// PROTOTYPE (Arium): Arium's own web page as the library. Throwaway.
Item {
    id: ariumWeb

    // How many times the page has asked to play, as last seen here
    property int playsSeen: 0
    property double lastPlayAt: 0

    // Runs in the page after every load. The page opens "arium-play://go" when Play is pressed on
    // a PC marked as set up; this marks the PC, stops that address leaving the page, and counts it.
    readonly property string hook: "(function(){" +
        "if(window.__ariumApp)return 'already';window.__ariumApp=true;window.__ariumPlay=0;" +
        "var fresh=false;try{if(localStorage.getItem('arium.moonlight')!=='yes'){localStorage.setItem('arium.moonlight','yes');fresh=true;}}catch(e){}" +
        "if(window.navigation){navigation.addEventListener('navigate',function(e){" +
        "if(String(e.destination.url).indexOf('arium-play:')===0){if(e.cancelable)e.preventDefault();window.__ariumPlay++;}});}" +
        "if(fresh){location.reload();return 'marked, reloading';}" +
        "return window.navigation?'hooked':'no navigation api';})()"

    // What the page itself does when Play is pressed (apps/web/src/lib/moonlight.ts)
    readonly property string pressPlay: "(function(){var a=document.createElement('a');a.href='arium-play://go';a.click();return 'pressed';})()"

    function playAsked(how) {
        var now = Date.now()
        if (now - lastPlayAt < 3000 || stackView.depth !== 1) {
            return
        }
        lastPlayAt = now
        arium.note("Play asked (" + how + ")")

        // Whatever the page is playing stops for the game
        web.runJavaScript("document.querySelectorAll('audio,video').forEach(function(m){m.pause()})")

        var component = Qt.createComponent("AriumStreamSegue.qml")
        stackView.push(component.createObject(stackView, {}), StackView.Immediate)
    }

    WebView {
        id: web
        anchors.fill: parent
        url: arium.url

        onLoadingChanged: function(request) {
            if (request.status === WebView.LoadSucceededStatus) {
                arium.note("loaded " + request.url)
                runJavaScript(hook, function(result) {
                    arium.note("hook: " + result)
                    if (arium.testPlayAfter > 0 && result !== "marked, reloading" && !testPlay.done) {
                        testPlay.done = true
                        testPlay.start()
                    }
                })
            }
            else if (request.status === WebView.LoadFailedStatus) {
                arium.note("load failed " + request.url + " " + request.errorString)
            }
        }

        // Only reached if the hook above could not stop the address leaving the page
        onUrlChanged: {
            if (url.toString().indexOf("arium-play:") === 0) {
                playAsked("address")
            }
        }
    }

    Timer {
        id: testPlay
        property bool done: false
        interval: arium.testPlayAfter * 1000
        onTriggered: web.runJavaScript(pressPlay, function(result) { arium.note("test: " + result) })
    }

    Timer {
        interval: 150
        repeat: true
        running: ariumWeb.visible
        onTriggered: web.runJavaScript("window.__ariumPlay||0", function(result) {
            var n = Number(result) || 0
            if (n > playsSeen) {
                playsSeen = n
                playAsked("page")
            }
            else if (n < playsSeen) {
                // The page was reloaded
                playsSeen = n
            }
        })
    }
}
