#pragma once

// PROTOTYPE (Arium): throwaway glue between Arium's web page and a stream.
// Answers one question: can the page sit inside this window and have Play start a stream natively?

#include <QObject>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>

#include "backend/computermanager.h"
#include "backend/nvcomputer.h"
#include "streaming/session.h"
#include "settings/streamingpreferences.h"

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
    QString m_Problem;
};
