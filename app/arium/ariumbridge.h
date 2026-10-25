#pragma once

// Arium: what the app's own screens (AriumWeb.qml, AriumStreamSegue.qml) need from the rest of
// Moonlight: finding the gaming PC, pairing with it without a PIN being shown, starting a stream
// of its "Arium" entry, how it streams (Arium's Settings page sets that), and a log.

#include <QObject>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
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
    Q_PROPERTY(bool streamOnly READ streamOnly CONSTANT)

public:
    explicit AriumBridge(QObject *parent = nullptr) : QObject(parent) {}

    // Where Arium is, and which page to open on
    QString url() const
    {
        return qEnvironmentVariable("ARIUM_URL", "https://arium.stevenhomeserver.com/games");
    }

    // Started only to stream the gaming PC's ARIUM_ENTRY (the Arium box's Steam tile, say): the
    // page stays out of sight, the stream starts at once, and the app closes when it ends
    bool streamOnly() const
    {
        return qEnvironmentVariable("ARIUM_STREAM_ONLY") == "1";
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

    // How this PC streams, for Arium's Settings page (StreamSettings in Arium's
    // apps/web/src/lib/ariumApp.ts), with what an automatic bitrate comes to on this screen now.
    // The resolution, frame rate and bitrate are Arium's own ("screen", 0 and 0 to suit the
    // screen); the rest are Moonlight's preferences.
    Q_INVOKABLE QString streamSettings(QWindow* window)
    {
        StreamingPreferences* prefs = StreamingPreferences::get();
        QSettings settings;
        QJsonObject chosen {
            {"resolution", resolutionSize(settings.value("arium/resolution").toString()).isValid() ? settings.value("arium/resolution").toString() : "screen"},
            {"fps", settings.value("arium/fps", 0).toInt()},
            {"bitrateKbps", settings.value("arium/bitrateKbps", 0).toInt()},
            {"codec", codecName(prefs->videoCodecConfig)},
            {"hdr", prefs->enableHdr},
            {"yuv444", prefs->enableYUV444},
            {"decoder", decoderName(prefs->videoDecoderSelection)},
            {"vsync", prefs->enableVsync},
            {"framePacing", prefs->framePacing},
            {"audio", audioName(prefs->audioConfig)},
            {"audioOnHost", prefs->playAudioOnHost},
            {"muteOnFocusLoss", prefs->muteOnFocusLoss},
            {"performanceOverlay", prefs->showPerformanceOverlay},
            {"connectionWarnings", prefs->connectionWarnings},
            {"swapFaceButtons", prefs->swapFaceButtons},
            {"multiController", prefs->multiController},
            {"gamepadMouse", prefs->gamepadMouse},
            {"absoluteMouse", prefs->absoluteMouseMode},
            {"swapMouseButtons", prefs->swapMouseButtons},
            {"reverseScroll", prefs->reverseScrollDirection},
            {"gameOptimizations", prefs->gameOptimizations},
        };
        int width = 1920, height = 1080, fps = 60;
        picture(window, width, height, fps);
        QJsonObject stream {
            {"settings", chosen},
            {"automaticKbps", StreamingPreferences::getDefaultBitrate(width, height, fps, prefs->enableYUV444)},
        };
        return QString::fromUtf8(QJsonDocument(stream).toJson(QJsonDocument::Compact));
    }

    // Keeps the settings the page asks for (as streamSettings() gives them), from the next stream
    // on. Anything missing or out of range is left as it was.
    Q_INVOKABLE void setStreamSettings(const QString& json)
    {
        const QJsonObject asked = QJsonDocument::fromJson(json.toUtf8()).object();
        if (asked.isEmpty()) return;
        StreamingPreferences* prefs = StreamingPreferences::get();
        QSettings settings;

        const QString resolution = asked["resolution"].toString();
        if (resolution == "screen" || resolutionSize(resolution).isValid()) settings.setValue("arium/resolution", resolution);
        const int fps = asked["fps"].toInt(-1);
        if (fps == 0 || (fps >= 30 && fps <= 240)) settings.setValue("arium/fps", fps);
        const int kbps = asked["bitrateKbps"].toInt(-1);
        if (kbps == 0 || (kbps >= 500 && kbps <= 500000)) settings.setValue("arium/bitrateKbps", kbps);

        const QString codec = asked["codec"].toString();
        if (codec == "auto") prefs->videoCodecConfig = StreamingPreferences::VCC_AUTO;
        else if (codec == "h264") prefs->videoCodecConfig = StreamingPreferences::VCC_FORCE_H264;
        else if (codec == "hevc") prefs->videoCodecConfig = StreamingPreferences::VCC_FORCE_HEVC;
        else if (codec == "av1") prefs->videoCodecConfig = StreamingPreferences::VCC_FORCE_AV1;
        const QString decoder = asked["decoder"].toString();
        if (decoder == "auto") prefs->videoDecoderSelection = StreamingPreferences::VDS_AUTO;
        else if (decoder == "hardware") prefs->videoDecoderSelection = StreamingPreferences::VDS_FORCE_HARDWARE;
        else if (decoder == "software") prefs->videoDecoderSelection = StreamingPreferences::VDS_FORCE_SOFTWARE;
        const QString audio = asked["audio"].toString();
        if (audio == "stereo") prefs->audioConfig = StreamingPreferences::AC_STEREO;
        else if (audio == "5.1") prefs->audioConfig = StreamingPreferences::AC_51_SURROUND;
        else if (audio == "7.1") prefs->audioConfig = StreamingPreferences::AC_71_SURROUND;

        const auto onOff = [&asked](const char* name, bool& preference) {
            if (asked[name].isBool()) preference = asked[name].toBool();
        };
        onOff("hdr", prefs->enableHdr);
        onOff("yuv444", prefs->enableYUV444);
        onOff("vsync", prefs->enableVsync);
        onOff("framePacing", prefs->framePacing);
        onOff("audioOnHost", prefs->playAudioOnHost);
        onOff("muteOnFocusLoss", prefs->muteOnFocusLoss);
        onOff("performanceOverlay", prefs->showPerformanceOverlay);
        onOff("connectionWarnings", prefs->connectionWarnings);
        onOff("swapFaceButtons", prefs->swapFaceButtons);
        onOff("multiController", prefs->multiController);
        onOff("gamepadMouse", prefs->gamepadMouse);
        onOff("absoluteMouse", prefs->absoluteMouseMode);
        onOff("swapMouseButtons", prefs->swapMouseButtons);
        onOff("reverseScroll", prefs->reverseScrollDirection);
        onOff("gameOptimizations", prefs->gameOptimizations);
        prefs->save();
        note("stream settings: " + json);
    }

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

    // The picture the stream is asked for, and a bitrate to suit, as chosen on Arium's Settings
    // page: by default the screen the app's window is on, at its own size and refresh rate, and
    // Moonlight's own bitrate for that. Set for each stream, so a different screen is matched.
    static void pictureFor(QWindow* window)
    {
        StreamingPreferences* prefs = StreamingPreferences::get();
        int width, height, fps;
        if (!picture(window, width, height, fps)) return;
        prefs->width = width;
        prefs->height = height;
        prefs->fps = fps;
        int chosen = QSettings().value("arium/bitrateKbps", 0).toInt();
        prefs->bitrateKbps = chosen > 0 ? chosen : StreamingPreferences::getDefaultBitrate(width, height, fps, prefs->enableYUV444);
        prefs->windowMode = StreamingPreferences::WM_FULLSCREEN_DESKTOP;
    }

    // The size and frame rate a stream would be asked for now; false if there is no screen to go by
    static bool picture(QWindow* window, int& width, int& height, int& fps)
    {
        QScreen* screen = window != nullptr ? window->screen() : nullptr;
        if (screen == nullptr) screen = QGuiApplication::primaryScreen();
        if (screen == nullptr) return false;
        QSize pixels = screen->size() * screen->devicePixelRatio();
        int rate = qRound(screen->refreshRate());
        width = pixels.width();
        height = pixels.height();
        fps = rate >= 30 ? rate : 60;
        QSize fixed = resolutionSize(QSettings().value("arium/resolution", "screen").toString());
        if (fixed.isValid()) {
            width = fixed.width();
            height = fixed.height();
        }
        int chosenFps = QSettings().value("arium/fps", 0).toInt();
        if (chosenFps > 0) fps = chosenFps;
        return true;
    }

    // A fixed resolution by its name on the Settings page; invalid for "screen" or anything else
    static QSize resolutionSize(const QString& name)
    {
        if (name == "720p") return QSize(1280, 720);
        if (name == "1080p") return QSize(1920, 1080);
        if (name == "1440p") return QSize(2560, 1440);
        if (name == "2160p") return QSize(3840, 2160);
        return QSize();
    }

    // The Settings page's names for Moonlight's choices
    static QString codecName(StreamingPreferences::VideoCodecConfig codec)
    {
        switch (codec) {
        case StreamingPreferences::VCC_FORCE_H264: return "h264";
        case StreamingPreferences::VCC_FORCE_HEVC:
        case StreamingPreferences::VCC_FORCE_HEVC_HDR_DEPRECATED: return "hevc";
        case StreamingPreferences::VCC_FORCE_AV1: return "av1";
        default: return "auto";
        }
    }

    static QString decoderName(StreamingPreferences::VideoDecoderSelection decoder)
    {
        switch (decoder) {
        case StreamingPreferences::VDS_FORCE_HARDWARE: return "hardware";
        case StreamingPreferences::VDS_FORCE_SOFTWARE: return "software";
        default: return "auto";
        }
    }

    static QString audioName(StreamingPreferences::AudioConfig audio)
    {
        switch (audio) {
        case StreamingPreferences::AC_51_SURROUND: return "5.1";
        case StreamingPreferences::AC_71_SURROUND: return "7.1";
        default: return "stereo";
        }
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
