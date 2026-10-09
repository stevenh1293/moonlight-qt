#include "sdlgamepadkeynavigation.h"

#include <QKeyEvent>
#include <QGuiApplication>
#include <QWindow>

#ifdef Q_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "settings/mappingmanager.h"

#define AXIS_NAVIGATION_REPEAT_DELAY 150

SdlGamepadKeyNavigation::SdlGamepadKeyNavigation(StreamingPreferences* prefs)
    : m_Prefs(prefs),
      m_Enabled(false),
      m_UiNavMode(false),
      m_FirstPoll(false),
      m_HasFocus(false),
      m_LastAxisNavigationEventTime(0)
{
    m_PollingTimer = new QTimer(this);
    connect(m_PollingTimer, &QTimer::timeout, this, &SdlGamepadKeyNavigation::onPollingTimerFired);
}

SdlGamepadKeyNavigation::~SdlGamepadKeyNavigation()
{
    disable();
}

void SdlGamepadKeyNavigation::enable()
{
    if (m_Enabled) {
        return;
    }

    // We have to initialize and uninitialize this in enable()/disable()
    // because we need to get out of the way of the Session class. If it
    // doesn't get to reinitialize the GC subsystem, it won't get initial
    // arrival events. Additionally, there's a race condition between
    // our QML objects being destroyed and SDL being deinitialized that
    // this solves too.
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) failed: %s",
                     SDL_GetError());
        return;
    }

    MappingManager mappingManager;
    mappingManager.applyMappings();

    // Drop all pending gamepad add events. SDL will generate these for us
    // on first init of the GC subsystem. We can't depend on them due to
    // overlapping lifetimes of SdlGamepadKeyNavigation instances, so we
    // will attach ourselves.
    //
    // NB: We use SDL_JoystickUpdate() instead of SDL_PumpEvents() because
    // the latter can do a bit more work that we want (like handling video
    // events that we intentionally do not want to process yet).
    SDL_JoystickUpdate();
    SDL_FlushEvent(SDL_CONTROLLERDEVICEADDED);

    // Open all currently attached game controllers
    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; i++) {
        if (SDL_IsGameController(i)) {
            SDL_GameController* gc = SDL_GameControllerOpen(i);
            if (gc != nullptr) {
                m_Gamepads.append(gc);
            }
        }
    }

    m_Enabled = true;

    // Start the polling timer if the window is focused
    updateTimerState();
}

void SdlGamepadKeyNavigation::disable()
{
    if (!m_Enabled) {
        return;
    }

    m_Enabled = false;
    updateTimerState();
    Q_ASSERT(!m_PollingTimer->isActive());

    while (!m_Gamepads.isEmpty()) {
        SDL_GameControllerClose(m_Gamepads[0]);
        m_Gamepads.removeAt(0);
    }

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void SdlGamepadKeyNavigation::notifyWindowFocus(bool hasFocus)
{
    m_HasFocus = hasFocus;
    updateTimerState();
}

void SdlGamepadKeyNavigation::setAriumMode(bool on)
{
    m_AriumMode = on;
    updateTimerState();
}

QString SdlGamepadKeyNavigation::describeGamepads()
{
    QStringList names;
    for (auto gc : std::as_const(m_Gamepads)) {
        names.append(QString::fromUtf8(SDL_GameControllerName(gc)));
    }
    return QString("%1 opened: %2").arg(names.size()).arg(names.join(" ; "));
}

// PROTOTYPE (Arium): every press as a name for the page; a held direction repeats.
void SdlGamepadKeyNavigation::pollForArium()
{
    SDL_Event event;
    SDL_JoystickUpdate();

    if (m_FirstPoll) {
        SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
        SDL_FlushEvent(SDL_CONTROLLERBUTTONUP);
        m_FirstPoll = false;
    }

    // Only while this app is the one in front: presses meant for something else are not ours
    bool inFront = true;
#ifdef Q_OS_WIN32
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    inFront = pid == GetCurrentProcessId();
#endif

    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1) {
        switch (event.type) {
        case SDL_QUIT:
            QCoreApplication::instance()->quit();
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        {
            const char* name = nullptr;
            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_A: name = "a"; break;
            case SDL_CONTROLLER_BUTTON_B: name = "b"; break;
            case SDL_CONTROLLER_BUTTON_X: name = "x"; break;
            case SDL_CONTROLLER_BUTTON_Y: name = "y"; break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: name = "lb"; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: name = "rb"; break;
            case SDL_CONTROLLER_BUTTON_BACK: name = "view"; break;
            case SDL_CONTROLLER_BUTTON_START: name = "menu"; break;
            case SDL_CONTROLLER_BUTTON_GUIDE: name = "guide"; break;
            default: break;
            }
            if (name != nullptr && inFront) {
                emit ariumPress(name);
            }
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
        {
            SDL_GameController* gc = SDL_GameControllerOpen(event.cdevice.which);
            if (gc != nullptr) {
                if (!m_Gamepads.contains(gc)) {
                    m_Gamepads.append(gc);
                    emit ariumPress("connected");
                }
                else {
                    SDL_GameControllerClose(gc);
                }
            }
            break;
        }
        }
    }

    // The D-pad and the left stick as one direction, repeating while held
    static const char* names[] = { "up", "down", "left", "right" };
    int direction = -1;
    for (auto gc : std::as_const(m_Gamepads)) {
        short x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        short y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP) || y < -20000) direction = 0;
        else if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN) || y > 20000) direction = 1;
        else if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || x < -20000) direction = 2;
        else if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || x > 20000) direction = 3;
        if (direction != -1) break;
    }
    // The right stick scrolls the page: how far it leans, every poll while it is held
    for (auto gc : std::as_const(m_Gamepads)) {
        short lean = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);
        if (inFront && (lean > 6500 || lean < -6500)) {
            emit ariumPress(QString("scroll:%1").arg(lean / 32767.0, 0, 'f', 2));
            break;
        }
    }

    Uint32 now = SDL_GetTicks();
    if (direction != m_AriumDirection) {
        m_AriumDirection = direction;
        m_AriumNextRepeat = now + 380;
        if (direction != -1 && inFront) emit ariumPress(names[direction]);
    }
    else if (direction != -1 && SDL_TICKS_PASSED(now, m_AriumNextRepeat)) {
        m_AriumNextRepeat = now + 120;
        if (inFront) emit ariumPress(names[direction]);
    }
}

void SdlGamepadKeyNavigation::onPollingTimerFired()
{
    if (m_AriumMode) {
        pollForArium();
        return;
    }

    SDL_Event event;

    // Update joystick state without pumping other events (see enable() comment)
    SDL_JoystickUpdate();

    // Discard any pending button events on the first poll to avoid picking up
    // stale input data from the stream session (like the quit combo).
    if (m_FirstPoll) {
        SDL_FlushEvent(SDL_CONTROLLERBUTTONDOWN);
        SDL_FlushEvent(SDL_CONTROLLERBUTTONUP);
        m_FirstPoll = false;
    }

    // Peep events rather than polling to avoid calling SDL_PumpEvents()
    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT) == 1) {
        switch (event.type) {
        case SDL_QUIT:
            // SDL may send us a quit event since we initialize
            // the video subsystem on startup. If we get one,
            // forward it on for Qt to take care of.
            QCoreApplication::instance()->quit();
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        {
            QEvent::Type type =
                    event.type == SDL_CONTROLLERBUTTONDOWN ?
                        QEvent::Type::KeyPress : QEvent::Type::KeyRelease;

            // Swap face buttons if needed
            if (m_Prefs->swapFaceButtons) {
                switch (event.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:
                    event.cbutton.button = SDL_CONTROLLER_BUTTON_B;
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    event.cbutton.button = SDL_CONTROLLER_BUTTON_A;
                    break;
                case SDL_CONTROLLER_BUTTON_X:
                    event.cbutton.button = SDL_CONTROLLER_BUTTON_Y;
                    break;
                case SDL_CONTROLLER_BUTTON_Y:
                    event.cbutton.button = SDL_CONTROLLER_BUTTON_X;
                    break;
                }
            }

            switch (event.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (m_UiNavMode) {
                    // Back-tab
                    sendKey(type, Qt::Key_Tab, Qt::ShiftModifier);
                }
                else {
                    sendKey(type, Qt::Key_Up);
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (m_UiNavMode) {
                    sendKey(type, Qt::Key_Tab);
                }
                else {
                    sendKey(type, Qt::Key_Down);
                }
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                sendKey(type, Qt::Key_Left);
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                sendKey(type, Qt::Key_Right);
                break;
            case SDL_CONTROLLER_BUTTON_A:
                if (m_UiNavMode) {
                    sendKey(type, Qt::Key_Space);
                }
                else {
                    sendKey(type, Qt::Key_Return);
                }
                break;
            case SDL_CONTROLLER_BUTTON_B:
                sendKey(type, Qt::Key_Escape);
                break;
            case SDL_CONTROLLER_BUTTON_X:
                sendKey(type, Qt::Key_Menu);
                break;
            case SDL_CONTROLLER_BUTTON_Y:
            case SDL_CONTROLLER_BUTTON_START:
                // HACK: We use this keycode to inform main.qml
                // to show the settings when Key_Menu is handled
                // by the control in focus.
                sendKey(type, Qt::Key_Hangup);
                break;
            default:
                break;
            }
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
            SDL_GameController* gc = SDL_GameControllerOpen(event.cdevice.which);
            if (gc != nullptr) {
                // SDL_CONTROLLERDEVICEADDED can be reported multiple times for the same
                // gamepad in rare cases, because SDL doesn't fixup the device index in
                // the SDL_CONTROLLERDEVICEADDED event if an unopened gamepad disappears
                // before we've processed the add event.
                if (!m_Gamepads.contains(gc)) {
                    m_Gamepads.append(gc);
                }
                else {
                    // We already have this game controller open
                    SDL_GameControllerClose(gc);
                }
            }
            break;
        }
    }

    // Handle analog sticks by polling
    for (auto gc : std::as_const(m_Gamepads)) {
        short leftX = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
        short leftY = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        if (SDL_GetTicks() - m_LastAxisNavigationEventTime < AXIS_NAVIGATION_REPEAT_DELAY) {
            // Do nothing
        }
        else if (leftY < -30000) {
            if (m_UiNavMode) {
                // Back-tab
                sendKey(QEvent::Type::KeyPress, Qt::Key_Tab, Qt::ShiftModifier);
                sendKey(QEvent::Type::KeyRelease, Qt::Key_Tab, Qt::ShiftModifier);
            }
            else {
                sendKey(QEvent::Type::KeyPress, Qt::Key_Up);
                sendKey(QEvent::Type::KeyRelease, Qt::Key_Up);
            }

            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }
        else if (leftY > 30000) {
            if (m_UiNavMode) {
                sendKey(QEvent::Type::KeyPress, Qt::Key_Tab);
                sendKey(QEvent::Type::KeyRelease, Qt::Key_Tab);
            }
            else {
                sendKey(QEvent::Type::KeyPress, Qt::Key_Down);
                sendKey(QEvent::Type::KeyRelease, Qt::Key_Down);
            }

            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }
        else if (leftX < -30000) {
            sendKey(QEvent::Type::KeyPress, Qt::Key_Left);
            sendKey(QEvent::Type::KeyRelease, Qt::Key_Left);
            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }
        else if (leftX > 30000) {
            sendKey(QEvent::Type::KeyPress, Qt::Key_Right);
            sendKey(QEvent::Type::KeyRelease, Qt::Key_Right);
            m_LastAxisNavigationEventTime = SDL_GetTicks();
        }
    }
}

void SdlGamepadKeyNavigation::sendKey(QEvent::Type type, Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    QGuiApplication* app = static_cast<QGuiApplication*>(QGuiApplication::instance());
    QWindow* focusWindow = app->focusWindow();
    if (focusWindow != nullptr) {
        QKeyEvent keyPressEvent(type, key, modifiers);
        app->sendEvent(focusWindow, &keyPressEvent);
    }
}

void SdlGamepadKeyNavigation::updateTimerState()
{
    // PROTOTYPE (Arium): Qt's own focus says nothing while the web view holds the keyboard
    bool wanted = m_Enabled && (m_HasFocus || m_AriumMode);
    if (m_PollingTimer->isActive() && !wanted) {
        m_PollingTimer->stop();
    }
    else if (!m_PollingTimer->isActive() && wanted) {
        // Flush events on the first poll
        m_FirstPoll = true;

        // Poll every 50 ms for a new joystick event (Arium: every 16, for a held direction)
        m_PollingTimer->start(m_AriumMode ? 16 : 50);
    }
}

void SdlGamepadKeyNavigation::setUiNavMode(bool uiNavMode)
{
    m_UiNavMode = uiNavMode;
}

int SdlGamepadKeyNavigation::getConnectedGamepads()
{
    Q_ASSERT(m_Enabled);

    int count = 0;
    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; i++) {
        if (SDL_IsGameController(i)) {
            count++;
        }
    }

    return count;
}
