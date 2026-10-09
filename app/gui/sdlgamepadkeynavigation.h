#pragma once

#include <QTimer>
#include <QEvent>

#include "SDL_compat.h"

#include "settings/streamingpreferences.h"

class SdlGamepadKeyNavigation : public QObject
{
    Q_OBJECT

public:
    SdlGamepadKeyNavigation(StreamingPreferences* prefs);

    ~SdlGamepadKeyNavigation();

    Q_INVOKABLE void enable();

    Q_INVOKABLE void disable();

    Q_INVOKABLE void notifyWindowFocus(bool hasFocus);

    Q_INVOKABLE void setUiNavMode(bool settingsMode);

    Q_INVOKABLE int getConnectedGamepads();

    // PROTOTYPE (Arium): presses are handed to Arium's page by name instead of being sent to
    // Moonlight's own screens as key presses, and are read whether or not Qt has keyboard focus
    // (the page's web view takes it).
    Q_INVOKABLE void setAriumMode(bool on);

    Q_INVOKABLE QString describeGamepads();

signals:
    void ariumPress(QString name);

private:
    void sendKey(QEvent::Type type, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

    void updateTimerState();

private slots:
    void onPollingTimerFired();

private:
    StreamingPreferences* m_Prefs;
    QTimer* m_PollingTimer;
    QList<SDL_GameController*> m_Gamepads;
    bool m_Enabled;
    bool m_UiNavMode;
    bool m_FirstPoll;
    bool m_HasFocus;
    bool m_AriumMode = false;
    int m_AriumDirection = -1;
    Uint32 m_AriumNextRepeat = 0;

    void pollForArium();
    Uint32 m_LastAxisNavigationEventTime;
};
