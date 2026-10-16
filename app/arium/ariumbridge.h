#pragma once

// Arium: what the app's own screens (AriumWeb.qml, AriumStreamSegue.qml) need from the rest of
// Moonlight: finding the gaming PC, pairing with it without a PIN being shown, starting a stream
// of its "Arium" entry, and a log.

#include <QObject>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QSysInfo>
#include <QRegularExpression>
#include <QWindow>

#include "backend/computermanager.h"
#include "backend/nvcomputer.h"
#include "backend/nvhttp.h"
#include "SDL_compat.h"
#include "streaming/session.h"
#include "settings/streamingpreferences.h"

// Set by the stream's controller handling when the middle button was what ended it (gamepad.cpp)
extern bool g_AriumLeftByMiddleButton;

class AriumBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString url READ url CONSTANT)
    Q_PROPERTY(int testPlayAfter READ testPlayAfter CONSTANT)
    Q_PROPERTY(QString testPresses READ testPresses CONSTANT)
    Q_PROPERTY(bool fullScreen READ fullScreen WRITE setFullScreen NOTIFY fullScreenChanged)

public:
    explicit AriumBridge(QObject *parent = nullptr) : QObject(parent) {}

    // Where Arium is, and which page to open on
    QString url() const
    {
        return qEnvironmentVariable("ARIUM_URL", "https://arium.stevenhomeserver.com/games");
    }

    // For trying it with no controller to hand: presses to hand the page, by name, comma-separated
    QString testPresses() const
    {
        return qEnvironmentVariable("ARIUM_TEST_PRESSES");
    }

    // For trying it with nobody at the keyboard: press Play this many seconds after the page loads
    int testPlayAfter() const
    {
        return qEnvironmentVariableIntValue("ARIUM_TEST_PLAY_AFTER");
    }

    // Full screen or a window, as last chosen (F11); full screen to begin with
    bool fullScreen() const
    {
        return QSettings().value("arium/fullScreen", true).toBool();
    }

    void setFullScreen(bool on)
    {
        if (on == fullScreen()) return;
        QSettings().setValue("arium/fullScreen", on);
        emit fullScreenChanged();
    }

    // Makes this PC ready to stream from the gaming PC at `host`: adds it if this app has not met
    // it, and pairs with it if they are not paired. Ends with ready(), or failed() with words for
    // the person. While pairing, pinToPass() asks for the PIN to be handed to Arium (by the page,
    // which is signed in), which hands it to the gaming PC; nobody types it.
    Q_INVOKABLE void prepare(ComputerManager* manager, const QString& host)
    {
        m_Manager = manager;
        m_Host = host;
        m_Added = false;
        m_Paired = false;
        disconnect(m_AddConnection);
        disconnect(m_PairConnection);
        if (host.isEmpty()) {
            emit failed("Arium doesn't know where the gaming PC is. Ask whoever runs Arium to give it the gaming PC's Sunshine login.");
            return;
        }
        step();
    }

    // A stream of the gaming PC's Arium entry, from what this app already knows about the PC.
    // (Moonlight's command-line launcher waits for the next change in the PC's state, which
    // inside an app that is already running took 28 seconds.) Null, with problem() set, if it cannot.
    Q_INVOKABLE Session* newSession(ComputerManager* manager, const QString& host, QWindow* window)
    {
        const QString entry = qEnvironmentVariable("ARIUM_ENTRY", "Arium");
        m_Problem.clear();

        NvComputer* computer = find(manager, host);
        if (computer == nullptr) {
            m_Problem = "The gaming PC isn't answering. It may be switched off.";
            return nullptr;
        }
        // Asked afresh: whether a game is already up decides between starting and rejoining,
        // and what the app last heard may be minutes old (it stops asking while idle).
        refresh(computer);
        QReadLocker lock(&computer->lock);
        if (computer->state != NvComputer::CS_ONLINE) {
            m_Problem = "The gaming PC isn't answering. It may be switched off.";
            return nullptr;
        }
        if (computer->pairState != NvComputer::PS_PAIRED) {
            m_Problem = "This PC couldn't be set up for games.";
            return nullptr;
        }
        for (const NvApp& app : std::as_const(computer->appList)) {
            if (app.name.compare(entry, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (computer->currentGameId != 0 && computer->currentGameId != app.id) {
                m_Problem = "The gaming PC is in use.";
                return nullptr;
            }
            NvApp chosen = app;
            lock.unlock();
            pictureFor(window);
            return new Session(computer, chosen, StreamingPreferences::get());
        }
        m_Problem = "The gaming PC has no entry called " + entry + " in Sunshine.";
        return nullptr;
    }

    Q_INVOKABLE QString problem() const { return m_Problem; }

    // For trying it with no controller to hand: leaves the stream as the middle button would,
    // ARIUM_TEST_LEAVE_AFTER seconds from now. Called just before a stream starts.
    Q_INVOKABLE void armTestLeave()
    {
        int seconds = qEnvironmentVariableIntValue("ARIUM_TEST_LEAVE_AFTER");
        if (seconds <= 0) return;
        SDL_InitSubSystem(SDL_INIT_TIMER);
        m_TestLeave = SDL_AddTimer(seconds * 1000, [](Uint32, void*) -> Uint32 {
            g_AriumLeftByMiddleButton = true;
            SDL_Event quit;
            quit.type = SDL_QUIT;
            quit.quit.timestamp = SDL_GetTicks();
            SDL_PushEvent(&quit);
            return 0;
        }, nullptr);
    }

    // Whether the stream that has just ended was left with the middle button (asked once).
    Q_INVOKABLE bool takeLeftByMiddleButton()
    {
        // A trial's pretend press that has not come yet must not land after the stream
        if (m_TestLeave != 0) {
            SDL_RemoveTimer(m_TestLeave);
            m_TestLeave = 0;
        }
        bool was = g_AriumLeftByMiddleButton;
        g_AriumLeftByMiddleButton = false;
        return was;
    }

    // Puts the mouse pointer in the bottom right corner of the screen it is on, where it cannot
    // be seen or rest on anything, when a controller is picked up. Moving the mouse brings it back.
    Q_INVOKABLE void parkPointer()
    {
        QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        if (screen == nullptr) screen = QGuiApplication::primaryScreen();
        QPoint corner = screen->geometry().bottomRight();
        if (QCursor::pos() != corner) QCursor::setPos(screen, corner);
    }

    // One line per event, so a run can be read afterwards without a debugger. The log starts
    // afresh once it passes a megabyte.
    Q_INVOKABLE void note(const QString& text)
    {
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        QFile f(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/arium.log");
        if (f.size() > 1024 * 1024) f.remove();
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream(&f) << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") << "  " << text << "\n";
        }
    }

signals:
    void ready();
    void failed(QString text);
    void pinToPass(QString pin, QString name);
    void fullScreenChanged();

private:
    void step()
    {
        NvComputer* computer = find(m_Manager, m_Host);
        if (computer == nullptr) {
            if (m_Added) {
                emit failed("The gaming PC isn't answering. It may be switched off.");
                return;
            }
            m_Added = true;
            note("adding the gaming PC at " + m_Host);
            m_AddConnection = connect(m_Manager, &ComputerManager::computerAddCompleted, this, [this](QVariant, QVariant) {
                disconnect(m_AddConnection);
                step();
            });
            m_Manager->addNewHostManually(m_Host);
            return;
        }

        // Asked afresh: a gaming PC just added or just paired with is not known to be paired, nor
        // its entries known, until the gaming PC has been asked again.
        refresh(computer);
        NvComputer::PairState pairState;
        {
            QReadLocker lock(&computer->lock);
            pairState = computer->pairState;
        }
        if (pairState == NvComputer::PS_PAIRED) {
            emit ready();
            return;
        }
        // Not known either way: the gaming PC has not answered, so pairing could not work either
        if (pairState != NvComputer::PS_NOT_PAIRED) {
            emit failed("The gaming PC isn't answering. It may be switched off.");
            return;
        }
        if (m_Paired) {
            emit failed("This PC couldn't be set up for games.");
            return;
        }
        m_Paired = true;

        QString pin = m_Manager->generatePinString();
        note("pairing with the gaming PC as " + pcName());
        m_PairConnection = connect(m_Manager, &ComputerManager::pairingCompleted, this, [this](NvComputer*, QString error) {
            disconnect(m_PairConnection);
            if (!error.isEmpty()) {
                note("pairing failed: " + error);
                emit failed("This PC couldn't be set up for games.");
                return;
            }
            note("paired");
            step();
        });
        // Pairing waits for the gaming PC to be given the PIN; the page hands it over meanwhile.
        m_Manager->pairHost(computer, pin);
        emit pinToPass(pin, pcName());
    }

    // What this PC is called in Sunshine's list: its own name, in the letters Arium accepts.
    static QString pcName()
    {
        QString name = QSysInfo::machineHostName();
        name.replace(QRegularExpression("[^\\w .'()-]"), "");
        name = name.left(40).trimmed();
        return "Arium app on " + (name.isEmpty() ? QString("a PC") : name);
    }

    // The gaming PC as this app knows it, by name or by any of its addresses; null if not met.
    static NvComputer* find(ComputerManager* manager, const QString& host)
    {
        for (NvComputer* computer : manager->getComputers()) {
            QReadLocker lock(&computer->lock);
            if (computer->name.compare(host, Qt::CaseInsensitive) == 0 ||
                    computer->activeAddress.address() == host ||
                    computer->localAddress.address() == host ||
                    computer->manualAddress.address() == host) {
                return computer;
            }
        }
        return nullptr;
    }

    // The picture the stream is asked for: the screen the app's window is on, at its own size and
    // refresh rate, and a bitrate to suit. Set for each stream, so a different screen is matched.
    static void pictureFor(QWindow* window)
    {
        QScreen* screen = window != nullptr ? window->screen() : nullptr;
        if (screen == nullptr) screen = QGuiApplication::primaryScreen();
        if (screen == nullptr) return;
        StreamingPreferences* prefs = StreamingPreferences::get();
        QSize pixels = screen->size() * screen->devicePixelRatio();
        int fps = qRound(screen->refreshRate());
        prefs->width = pixels.width();
        prefs->height = pixels.height();
        prefs->fps = fps >= 30 ? fps : 60;
        prefs->bitrateKbps = StreamingPreferences::getDefaultBitrate(prefs->width, prefs->height, prefs->fps, prefs->enableYUV444);
        prefs->windowMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;
    }

    static void refresh(NvComputer* computer)
    {
        NvAddress address;
        uint16_t httpsPort;
        QSslCertificate cert;
        bool notNvidia;
        QString uuid;
        {
            QReadLocker lock(&computer->lock);
            // Before background polling has reached the gaming PC it has no active address yet:
            // the one it was added by, or the last it was seen at, is used instead.
            address = computer->activeAddress;
            if (address.isNull()) address = computer->manualAddress;
            if (address.isNull()) address = computer->localAddress;
            httpsPort = computer->activeHttpsPort;
            cert = computer->serverCert;
            notNvidia = !computer->isNvidiaServerSoftware;
            uuid = computer->uuid;
        }
        if (address.isNull()) return;
        try {
            NvHTTP http(address, 0, cert, notNvidia);
            NvComputer now(http, http.getServerInfo(NvHTTP::NvLogLevel::NVLL_NONE, true));
            if (now.uuid != uuid) return;
            // And its entries, which only a paired PC is told; update() takes them with the rest
            if (now.pairState == NvComputer::PS_PAIRED) {
                try {
                    // From what was read under the lock, and the port the gaming PC has just given
                    NvHTTP paired(address, now.activeHttpsPort != 0 ? now.activeHttpsPort : httpsPort, now.serverCert.isNull() ? cert : now.serverCert, notNvidia);
                    now.appList = paired.getAppList();
                } catch (...) {
                    // What was last heard stands
                }
            }
            computer->update(now);
        } catch (...) {
            // Out of reach just now: what was last heard stands, and starting the stream will say so
        }
    }

    QString m_Problem;
    SDL_TimerID m_TestLeave = 0;
    ComputerManager* m_Manager = nullptr;
    QString m_Host;
    bool m_Added = false;
    bool m_Paired = false;
    QMetaObject::Connection m_AddConnection;
    QMetaObject::Connection m_PairConnection;
};
