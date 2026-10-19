#pragma once

// Arium: keeps the app up to date. The page, signed in to Arium, hands over the newest build Arium
// keeps (its version, size, SHA-256 and a link good for half an hour); a newer one is downloaded
// in the background into the app's own folder and checked. It is installed the next time the app
// opens, behind the "Updating" screen, by running its installer silently; the installer opens the
// app again when it is done. "Update now" on the page does the same at once, never mid-stream.
// An installer that ran and left the app as it was is not tried again: the page then offers the
// app's page on the store instead.

#include <QObject>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <QVersionNumber>

#include "ariumbridge.h"

class AriumUpdater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    // "" nothing to do, "downloading", "ready" (installed at the next start, or by Update now),
    // or "failed" (an installer was run and the app is still the old one)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

public:
    AriumUpdater(AriumBridge* bridge, QObject* parent = nullptr) : QObject(parent), m_Bridge(bridge)
    {
        const QVersionNumber own = QVersionNumber::fromString(version());
        const QVersionNumber tried = QVersionNumber::fromString(QSettings().value("arium/update/tried").toString());
        const QVersionNumber held = QVersionNumber::fromString(QSettings().value("arium/update/version").toString());
        if (!held.isNull() && held <= own) {
            // Installed (or older than what is here): what was downloaded for it goes.
            forget();
        }
        else if (!tried.isNull() && tried == held) {
            m_Bridge->note("the update to " + held.toString() + " was run before and the app is still " + version() + ": not trying it again");
            m_State = "failed";
        }
        else if (!held.isNull() && checked()) {
            m_State = "ready";
        }
        if (!tried.isNull() && tried <= own) QSettings().remove("arium/update/tried");
    }

    QString version() const { return QStringLiteral(ARIUM_VERSION_STR); }
    QString state() const { return m_State; }

    // What the page says Arium keeps, as JSON ({version, size, sha256, url}); `origin` is the
    // page's own, which the link is under. Downloaded if newer than this app and not already here.
    Q_INVOKABLE void offered(const QString& json, const QString& origin)
    {
#ifndef Q_OS_WIN32
        // Arium's builds are Windows installers. The Linux box is updated from the server instead
        // (arium-box's arium-push), so nothing is downloaded and the page never offers Update now.
        Q_UNUSED(json);
        Q_UNUSED(origin);
        return;
#endif
        const QJsonObject build = QJsonDocument::fromJson(json.toUtf8()).object();
        const QVersionNumber offeredVersion = QVersionNumber::fromString(build.value("version").toString());
        const QString sha256 = build.value("sha256").toString();
        const qint64 size = build.value("size").toVariant().toLongLong();
        const QUrl url = QUrl(origin).resolved(QUrl(build.value("url").toString()));
        if (offeredVersion.isNull() || offeredVersion <= QVersionNumber::fromString(version())) return;
        if (sha256.length() != 64 || size <= 0 || size > 512LL * 1024 * 1024) return;
        // Only from Arium itself, over https
        if (url.scheme() != "https" || url.host() != QUrl(origin).host()) return;
        if (m_State == "downloading") return;
        // The version whose installer ran and left the app as it was is not tried again; a newer one is
        if (m_State == "failed" && offeredVersion <= QVersionNumber::fromString(QSettings().value("arium/update/tried").toString())) return;
        // A download that did not come whole is tried again ten minutes later, not at once
        if (m_LastFailure.isValid() && m_LastFailure.secsTo(QDateTime::currentDateTimeUtc()) < 10 * 60) return;
        if (m_State == "ready" && QSettings().value("arium/update/sha256").toString() == sha256) return;

        forget();
        m_Expected = sha256;
        m_ExpectedSize = size;
        m_Version = offeredVersion.toString();
        QDir().mkpath(folder());
        m_Part.reset(new QFile(installer() + ".part"));
        if (!m_Part->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            m_Bridge->note("the update could not be saved in " + folder() + "; it is tried again in ten minutes");
            m_Part.reset();
            m_LastFailure = QDateTime::currentDateTimeUtc();
            setState("");
            return;
        }
        m_Hash.reset();
        setState("downloading");
        m_Bridge->note("downloading the update to " + m_Version);
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setTransferTimeout(10 * 60 * 1000);
        m_Reply = m_Network.get(request);
        connect(m_Reply, &QNetworkReply::readyRead, this, [this]() { take(); });
        connect(m_Reply, &QNetworkReply::finished, this, [this]() { finished(); });
    }

    // Runs the installer that is ready: true once it has started (the app should then close).
    // Its bytes are checked again first. Not run twice for one version: see the constructor.
    Q_INVOKABLE bool apply()
    {
#ifndef Q_OS_WIN32
        // Nothing is ever downloaded here (see offered())
        return false;
#endif
        if (m_State != "ready" || !checked()) {
            if (m_State == "ready") setState("");
            return false;
        }
        const QString held = QSettings().value("arium/update/version").toString();
        QSettings().setValue("arium/update/tried", held);
        QSettings().sync();
        m_Bridge->note("installing the update to " + held);
        const bool started = QProcess::startDetached(QDir::toNativeSeparators(installer()),
            { "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/ARIUMUPDATE" });
        if (!started) {
            m_Bridge->note("the update's installer could not be started");
            setState("failed");
        }
        return started;
    }

signals:
    void stateChanged();

private:
    static QString folder()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/updates";
    }

    static QString installer()
    {
        return folder() + "/Arium-Setup.exe";
    }

    // The installer that was downloaded, as it was when it was checked
    static bool checked()
    {
        QFile file(installer());
        if (!file.open(QIODevice::ReadOnly)) return false;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&file)) return false;
        return QString::fromLatin1(hash.result().toHex()) == QSettings().value("arium/update/sha256").toString();
    }

    static void forget()
    {
        QFile::remove(installer());
        QFile::remove(installer() + ".part");
        QSettings().remove("arium/update/version");
        QSettings().remove("arium/update/sha256");
    }

    void setState(const QString& state)
    {
        if (state == m_State) return;
        m_State = state;
        emit stateChanged();
    }

    void take()
    {
        const QByteArray bytes = m_Reply->readAll();
        m_Written += bytes.size();
        if (m_Written > m_ExpectedSize) {
            m_Reply->abort();
            return;
        }
        m_Hash.addData(bytes);
        m_Part->write(bytes);
    }

    void finished()
    {
        take();
        const bool fetched = m_Reply->error() == QNetworkReply::NoError &&
            m_Reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        m_Reply->deleteLater();
        m_Reply = nullptr;
        m_Part->close();
        const bool whole = fetched && m_Written == m_ExpectedSize && QString::fromLatin1(m_Hash.result().toHex()) == m_Expected;
        m_Written = 0;
        if (!whole || !QFile::rename(m_Part->fileName(), installer())) {
            m_Bridge->note("the update to " + m_Version + " did not download whole; it is tried again in ten minutes");
            QFile::remove(m_Part->fileName());
            m_Part.reset();
            m_LastFailure = QDateTime::currentDateTimeUtc();
            setState("");
            return;
        }
        m_Part.reset();
        QSettings().setValue("arium/update/version", m_Version);
        QSettings().setValue("arium/update/sha256", m_Expected);
        m_Bridge->note("the update to " + m_Version + " is ready; it is installed the next time the app opens");
        setState("ready");
    }

    AriumBridge* m_Bridge;
    QNetworkAccessManager m_Network;
    QNetworkReply* m_Reply = nullptr;
    QScopedPointer<QFile> m_Part;
    QCryptographicHash m_Hash { QCryptographicHash::Sha256 };
    QString m_Expected;
    qint64 m_ExpectedSize = 0;
    qint64 m_Written = 0;
    QString m_Version;
    QString m_State;
    QDateTime m_LastFailure;
};
