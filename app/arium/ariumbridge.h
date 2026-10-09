#pragma once

// PROTOTYPE (Arium): throwaway glue between Arium's web page and a stream.
// Answers one question: can the page sit inside this window and have Play start a stream natively?

#include <QObject>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

#include "backend/computermanager.h"
#include "backend/nvcomputer.h"
#include "backend/nvhttp.h"
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

    // A stream of the gaming PC's Arium entry, from what this app already knows about the PC.
    // (Moonlight's command-line launcher waits for the next change in the PC's state, which
    // inside an app that is already running took 28 seconds.) Null, with problem() set, if it cannot.
    Q_INVOKABLE Session* newSession(ComputerManager* manager)
    {
        const QString host = qEnvironmentVariable("ARIUM_HOST", "GamingVM");
        const QString entry = qEnvironmentVariable("ARIUM_ENTRY", "Arium");
        m_Problem.clear();

        for (NvComputer* computer : manager->getComputers()) {
            // Asked afresh: whether a game is already up decides between starting and rejoining,
            // and what the app last heard may be minutes old (it stops asking while idle).
            refresh(computer);
            QReadLocker lock(&computer->lock);
            if (computer->name.compare(host, Qt::CaseInsensitive) != 0 &&
                    computer->activeAddress.address() != host &&
                    computer->localAddress.address() != host &&
                    computer->manualAddress.address() != host) {
                continue;
            }
            if (computer->state != NvComputer::CS_ONLINE) {
                m_Problem = "The gaming PC is not answering";
                return nullptr;
            }
            if (computer->pairState != NvComputer::PS_PAIRED) {
                m_Problem = "This PC has not been paired with the gaming PC";
                return nullptr;
            }
            for (const NvApp& app : std::as_const(computer->appList)) {
                if (app.name.compare(entry, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                if (computer->currentGameId != 0 && computer->currentGameId != app.id) {
                    m_Problem = "Something else is already running on the gaming PC";
                    return nullptr;
                }
                NvApp chosen = app;
                lock.unlock();
                return new Session(computer, chosen, StreamingPreferences::get());
            }
            m_Problem = "The gaming PC has no entry called " + entry;
            return nullptr;
        }

        m_Problem = "The gaming PC (" + host + ") has not been found yet";
        return nullptr;
    }

    Q_INVOKABLE QString problem() const { return m_Problem; }

    // Whether the stream that has just ended was left with the middle button (asked once).
    Q_INVOKABLE bool takeLeftByMiddleButton()
    {
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

    // One line per event, so a run can be read afterwards without a debugger
    Q_INVOKABLE void note(const QString& text)
    {
        QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
        QFile f(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/arium-prototype.log");
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream(&f) << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") << "  " << text << "\n";
        }
    }

private:
    static void refresh(NvComputer* computer)
    {
        NvAddress address;
        QSslCertificate cert;
        bool notNvidia;
        QString uuid;
        {
            QReadLocker lock(&computer->lock);
            address = computer->activeAddress;
            cert = computer->serverCert;
            notNvidia = !computer->isNvidiaServerSoftware;
            uuid = computer->uuid;
        }
        if (address.isNull()) return;
        try {
            NvHTTP http(address, 0, cert, notNvidia);
            NvComputer now(http, http.getServerInfo(NvHTTP::NvLogLevel::NVLL_NONE, true));
            if (now.uuid == uuid) computer->update(now);
        } catch (...) {
            // Out of reach just now: what was last heard stands, and starting the stream will say so
        }
    }

    QString m_Problem;
};
