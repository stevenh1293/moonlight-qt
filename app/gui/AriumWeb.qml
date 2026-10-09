import QtQuick 2.9
import QtQuick.Controls 2.2
import QtWebView

import SdlGamepadKeyNavigation 1.0

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
        "window.addEventListener('keydown',function(e){if((e.ctrlKey&&(e.key==='q'||e.key==='Q'))||(e.altKey&&e.key==='F4'))window.__ariumExit=1;},true);" +
        // A way out that is always on screen: this button, except where the page's own panel offers Exit
        "var st=document.createElement('style');st.textContent='html[data-arium-panel] #arium-exit{display:none}';document.documentElement.appendChild(st);" +
        "var x=document.createElement('button');x.id='arium-exit';x.textContent='\u2715  Exit';x.title='Close Arium';" +
        "x.style.cssText='position:fixed;top:10px;right:14px;z-index:2147483647;padding:9px 16px;border-radius:999px;border:1px solid rgba(255,255,255,.4);background:rgba(20,23,29,.9);color:#e9ebf0;font:600 15px system-ui,sans-serif;cursor:pointer';" +
        "x.onclick=function(){window.__ariumExit=1;};document.documentElement.appendChild(x);" +
        "if(fresh){location.reload();return 'marked, reloading';}" +
        "return (window.navigation?'hooked':'no navigation api')+' | secure context: '+window.isSecureContext+' | getGamepads: '+(typeof navigator.getGamepads);})()"

    // What the page itself does when Play is pressed (apps/web/src/lib/moonlight.ts)
    readonly property string pressPlay: "(function(){var a=document.createElement('a');a.href='arium-play://go';a.click();return 'pressed';})()"

    // Sends the page to one of its own addresses without loading it afresh
    function go(path) {
        web.runJavaScript("window.__ariumGo?window.__ariumGo('" + path + "'):location.assign('" + path + "')")
    }

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

    // Each press of the controller, read by the app, goes to the page by name. The page need not
    // have keyboard focus for this, which a web view's own controller support would require.
    property int pressesNoted: 0
    Connections {
        target: SdlGamepadKeyNavigation
        function onAriumPress(name) {
            arium.parkPointer()
            if (pressesNoted < 40 && name.indexOf("scroll:") !== 0) {
                pressesNoted++
                arium.note("press: " + name + (name === "connected" ? " -> " + SdlGamepadKeyNavigation.describeGamepads() : ""))
            }
            if (stackView.depth === 1) {
                web.runJavaScript("window.__ariumPad&&window.__ariumPad('" + name + "')")
            }
        }
    }

    // Pretend presses, one every 600 ms from 8 s after start, then what the page has highlighted
    Timer {
        property var left: arium.testPresses ? arium.testPresses.split(",") : []
        interval: left.length === arium.testPresses.split(",").length ? 8000 : 600
        running: arium.testPresses !== "" && left.length > 0
        repeat: true
        onTriggered: {
            var name = left.shift()
            leftChanged()
            web.runJavaScript("window.__ariumPad&&window.__ariumPad('" + name + "')")
            web.runJavaScript("(function(){var r=document.querySelector('[data-pad-focus], .ring-4');return (window.__ariumPad?'':'NO HANDLER ')+(r?String(r.getAttribute('aria-label')||r.innerText||r.tagName).replace(/\\s+/g,' ').slice(0,50):'nothing highlighted')+' | '+location.pathname+location.search})()", function(result) {
                arium.note("test press " + name + " -> " + result)
            })
        }
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
                    padReport.restart()
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

    // What the page can see of the controllers, noted now and then for the log
    Timer {
        id: padReport
        interval: 5000
        repeat: true
        property string last: ""
        onTriggered: web.runJavaScript("(function(){try{var p=[].slice.call(navigator.getGamepads()).filter(Boolean);return p.length+' pad(s): '+p.map(function(g){return g.id+' mapping='+g.mapping+' buttons='+g.buttons.length+' pressed='+g.buttons.map(function(b,i){return b.pressed?i:''}).filter(String).join('+')}).join(' ; ')+' | focus: '+document.hasFocus()}catch(e){return 'error: '+e}})()", function(result) {
            if (result !== last) {
                last = result
                arium.note("controllers: " + result)
            }
        })
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
        // The page asks to leave the app by setting __ariumExit (its menu's Exit, or Ctrl+Q)
        onTriggered: web.runJavaScript("(window.__ariumPlay||0)+','+(window.__ariumExit||0)", function(result) {
            var parts = String(result).split(",")
            if (Number(parts[1]) > 0) {
                arium.note("exit asked by the page")
                Qt.quit()
                return
            }
            var n = Number(parts[0]) || 0
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
