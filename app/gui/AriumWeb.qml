import QtQuick 2.9
import QtQuick.Controls 2.2
import QtWebView

import SdlGamepadKeyNavigation 1.0

// Arium's own web page as the app's library: the whole of Arium as the page draws it. The page and
// the app talk through a few names on the page's window (apps/web/src/lib/ariumApp.ts in Arium).
Item {
    id: ariumWeb

    // How many times the page has asked to play, and to switch between full screen and a window, as last seen here
    property int playsSeen: 0
    property int fullSeen: 0
    property double lastPlayAt: 0

    // Runs in the page after every load: marks it as being in the app, and gives it the app's own
    // keys (Ctrl+Q and Alt+F4 close the app, F11 switches between full screen and a window) and a
    // way out that is always on screen.
    readonly property string hook: "(function(){" +
        "if(window.__ariumApp)return 'already';window.__ariumApp=true;window.__ariumPlay=0;window.__ariumFull=0;" +
        "window.addEventListener('keydown',function(e){" +
        "if((e.ctrlKey&&(e.key==='q'||e.key==='Q'))||(e.altKey&&e.key==='F4')){e.preventDefault();window.__ariumExit=1;}" +
        "if(e.key==='F11'){e.preventDefault();window.__ariumFull++;}},true);" +
        // A way out that is always on screen: this button, except where the page's own panel offers Exit
        "var st=document.createElement('style');st.textContent='html[data-arium-panel] #arium-exit{display:none}';document.documentElement.appendChild(st);" +
        "var x=document.createElement('button');x.id='arium-exit';x.textContent='\u2715  Exit';x.title='Close Arium';" +
        "x.style.cssText='position:fixed;top:10px;right:14px;z-index:2147483647;padding:9px 16px;border-radius:999px;border:1px solid rgba(255,255,255,.4);background:rgba(20,23,29,.9);color:#e9ebf0;font:600 15px system-ui,sans-serif;cursor:pointer';" +
        "x.onclick=function(){window.__ariumExit=1;};document.documentElement.appendChild(x);" +
        "return 'hooked';})()"

    // For trying it with nobody at the keyboard: the page's Play button on the game screen showing
    readonly property string pressPlay: "(function(){var b=[].slice.call(document.querySelectorAll('button')).filter(function(x){return x.offsetParent!==null&&/^(Play|Continue)/.test((x.innerText||'').trim())})[0];if(b){b.click();return 'pressed '+b.innerText.trim()}return 'no Play button on '+location.pathname})()"

    // Sends the page to one of its own addresses without loading it afresh
    function go(path) {
        web.runJavaScript("window.__ariumGo?window.__ariumGo('" + path + "'):location.assign('" + path + "')")
    }

    // Hands the gaming PC a pairing PIN through the page, which is signed in to Arium; `told` is
    // given how that went ("sent", or why not) once the page knows, or "" if it never says.
    function passPin(pin, name, told) {
        web.runJavaScript("window.__ariumPair?(window.__ariumPair(" + JSON.stringify(pin) + "," + JSON.stringify(name) + "),'asked'):'not signed in'", function(result) {
            if (result !== "asked") {
                told("Sign in to Arium first.")
                return
            }
            pairOutcome.told = told
            pairOutcome.tries = 0
            pairOutcome.start()
        })
    }

    Timer {
        id: pairOutcome
        property var told
        property int tries: 0
        interval: 250
        repeat: true
        onTriggered: web.runJavaScript("window.__ariumPairSent||''", function(result) {
            if (result || ++tries > 40) {
                stop()
                told(result || "")
            }
        })
    }

    function playAsked(gamingPc) {
        var now = Date.now()
        if (now - lastPlayAt < 3000 || stackView.depth !== 1) {
            return
        }
        lastPlayAt = now
        arium.note("Play asked; the gaming PC is at " + gamingPc)

        // Whatever the page is playing stops for the game
        web.runJavaScript("document.querySelectorAll('audio,video').forEach(function(m){m.pause()})")

        var component = Qt.createComponent("AriumStreamSegue.qml")
        stackView.push(component.createObject(stackView, { gamingPc: gamingPc }), StackView.Immediate)
    }

    // Each press of the controller, read by the app, goes to the page by name. The page need not
    // have keyboard focus for this, which a web view's own controller support would require.
    property int pressesNoted: 0
    Connections {
        target: SdlGamepadKeyNavigation
        function onAriumPress(name) {
            var coming = name === "connected" || name === "disconnected" || name.indexOf("released") === 0
            if (name !== "disconnected" && name.indexOf("released") !== 0) arium.parkPointer()
            if ((pressesNoted < 40 || coming) && name.indexOf("scroll:") !== 0) {
                pressesNoted++
                arium.note("press: " + name + (coming ? " -> " + SdlGamepadKeyNavigation.describeGamepads() : ""))
            }
            if (stackView.depth === 1 && name !== "disconnected" && name.indexOf("released") !== 0) {
                web.runJavaScript("window.__ariumPad&&window.__ariumPad('" + name + "')")
            }
        }
    }

    // Pretend presses, one every 600 ms from 8 s after start, then what the page has highlighted
    Timer {
        property var left: arium.testPresses ? arium.testPresses.split(",") : []
        interval: left.length === arium.testPresses.split(",").length ? 8000 : 600
        running: arium.testPresses !== "" && left.length > 0 && stackView.depth === 1
        repeat: true
        onTriggered: {
            var name = left.shift()
            leftChanged()
            if (name.indexOf("click=") === 0) {
                // "click=Play_Star_Fox": presses the first button or link whose name or words start so
                var wanted = name.substring(6).replace(/_/g, " ")
                web.runJavaScript("(function(){var w=" + JSON.stringify(wanted) + ";var all=[].slice.call(document.querySelectorAll('button,a'));var e=all.filter(function(x){return x.offsetParent!==null&&((x.getAttribute('aria-label')||'').indexOf(w)===0||(x.innerText||'').trim().indexOf(w)===0)})[0];if(e){e.click();return 'clicked '+w+' | '+location.pathname}return 'NOT FOUND '+w+' | '+location.pathname})()", function(result) {
                    arium.note("test " + result)
                })
                return
            }
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
                    if (arium.testPlayAfter > 0 && !testPlay.done) {
                        testPlay.done = true
                        testPlay.start()
                    }
                })
            }
            else if (request.status === WebView.LoadFailedStatus) {
                arium.note("load failed " + request.url + " " + request.errorString)
            }
        }

    }

    // The page's focus and sound, noted now and then for the log. The page is never asked about
    // controllers here: asking makes the web view take hold of them as well.
    Timer {
        id: padReport
        interval: 5000
        repeat: true
        property string last: ""
        onTriggered: web.runJavaScript("(function(){return 'focus: '+document.hasFocus()+' | sound: '+(window.__ariumSound||'not used yet')})()", function(result) {
            if (result !== last) {
                last = result
                arium.note("page: " + result)
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
        // The page's marks: Play (counted up once per press, with where the gaming PC is), Exit
        // (its menu's Exit, or Ctrl+Q) and F11
        onTriggered: web.runJavaScript("JSON.stringify([window.__ariumPlay||0,window.__ariumExit||0,window.__ariumFull||0,window.__ariumGamingPc||''])", function(result) {
            var marks
            try { marks = JSON.parse(result) } catch (e) { return }
            if (marks[1] > 0) {
                arium.note("exit asked by the page")
                Qt.quit()
                return
            }
            // Counts lower than last seen: the page was loaded afresh
            if (marks[2] > fullSeen) arium.fullScreen = !arium.fullScreen
            fullSeen = marks[2]
            if (marks[0] > playsSeen) {
                playsSeen = marks[0]
                playAsked(marks[3])
            }
            playsSeen = marks[0]
        })
    }
}
