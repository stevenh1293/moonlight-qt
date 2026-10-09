#pragma once

// PROTOTYPE (Arium): throwaway glue between Arium's web page and a stream.
// Answers one question: can the page sit inside this window and have Play start a stream natively?

#include <QObject>
#include <QFile>
#include <QDateTime>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>

#include "cli/startstream.h"
#include "settings/streamingpreferences.h"

class AriumBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString url READ url CONSTANT)

public:
    explicit AriumBridge(QObject *parent = nullptr) : QObject(parent) {}

    // Where Arium is, and which page to open on
    QString url() const
    {
        return qEnvironmentVariable("ARIUM_URL", "https://arium.stevenhomeserver.com/games");
    }

    // A launcher is good for one stream only, so every Play gets a new one
    Q_INVOKABLE QObject* newLauncher()
    {
        return new CliStartStream::Launcher(qEnvironmentVariable("ARIUM_HOST", "GamingVM"),
                                            qEnvironmentVariable("ARIUM_ENTRY", "Arium"),
                                            StreamingPreferences::get(), this);
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
};
