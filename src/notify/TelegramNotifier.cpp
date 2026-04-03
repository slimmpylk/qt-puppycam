#include "TelegramNotifier.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtNetwork/QHttpMultiPart>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

TelegramNotifier::TelegramNotifier(const QString& token,
                                   const QString& chatId,
                                   QObject*       parent)
    : QObject(parent), token_(token), chatId_(chatId) {}

bool TelegramNotifier::isConfigured() const {
    return !token_.isEmpty() && !chatId_.isEmpty();
}

// ── public slot ───────────────────────────────────────────────────────────────

void TelegramNotifier::onClipReady(const QString&    mp4Path,
                                   const QString&    reason,
                                   const QByteArray& snapshot)
{
    if (!isConfigured()) {
        qInfo() << "TelegramNotifier: not configured — set PUPPYCAM_TELEGRAM_TOKEN"
                   " and PUPPYCAM_TELEGRAM_CHAT_ID in /etc/puppycam.env";
        // Still delete the clip so SD card doesn't fill up
        QFile::remove(mp4Path);
        return;
    }

    const QString caption =
        QStringLiteral("🐾 puppycam alert!\nTrigger: %1").arg(reason);

    // 1. Send snapshot photo immediately so you get a fast notification
    if (!snapshot.isEmpty())
        sendPhoto(snapshot, caption);
    else
        sendMessage(caption);

    // 2. Upload the full clip — deletes local file after confirmed upload
    sendVideoAndDelete(mp4Path,
                       QStringLiteral("📹 Full clip (%1)").arg(reason));
}

// ── send helpers ──────────────────────────────────────────────────────────────

void TelegramNotifier::sendMessage(const QString& text)
{
    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.telegram.org/bot%1/sendMessage").arg(token_)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const QString safe = QString(text).replace('\\', "\\\\")
                             .replace('"', "\\\"")
                             .replace('\n', "\\n");
    const QByteArray body =
        QStringLiteral("{\"chat_id\":\"%1\",\"text\":\"%2\"}")
            .arg(chatId_, safe).toUtf8();

    auto* reply = nam_.post(req, body);
    connect(reply, &QNetworkReply::finished, reply, [reply] {
        if (reply->error() != QNetworkReply::NoError)
            qWarning() << "Telegram sendMessage failed:" << reply->errorString();
        reply->deleteLater();
    });
}

void TelegramNotifier::sendPhoto(const QByteArray& jpeg, const QString& caption)
{
    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.telegram.org/bot%1/sendPhoto").arg(token_)));

    auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addField = [&](const QByteArray& name, const QByteArray& value) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"%1\"")
                        .arg(QString::fromLatin1(name)));
        p.setBody(value);
        mp->append(p);
    };

    addField("chat_id", chatId_.toUtf8());
    addField("caption", caption.toUtf8());

    QHttpPart photo;
    photo.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"photo\"; filename=\"snapshot.jpg\""));
    photo.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/jpeg"));
    photo.setBody(jpeg);
    mp->append(photo);

    auto* reply = nam_.post(req, mp);
    mp->setParent(reply);
    connect(reply, &QNetworkReply::finished, reply, [reply] {
        if (reply->error() != QNetworkReply::NoError)
            qWarning() << "Telegram sendPhoto failed:" << reply->errorString();
        else
            qInfo() << "Telegram: snapshot sent";
        reply->deleteLater();
    });
}

void TelegramNotifier::sendVideoAndDelete(const QString& path,
                                          const QString& caption)
{
    auto* file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning() << "TelegramNotifier: cannot open clip file" << path;
        delete file;
        return;
    }

    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.telegram.org/bot%1/sendVideo").arg(token_)));

    auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addField = [&](const QByteArray& name, const QByteArray& value) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"%1\"")
                        .arg(QString::fromLatin1(name)));
        p.setBody(value);
        mp->append(p);
    };

    addField("chat_id", chatId_.toUtf8());
    addField("caption", caption.toUtf8());
    addField("supports_streaming", "true");

    // setBodyDevice streams the file directly — no full load into RAM
    QHttpPart video;
    video.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"video\"; filename=\"clip.mp4\""));
    video.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("video/mp4"));
    video.setBodyDevice(file);  // file will be read chunk by chunk
    file->setParent(mp);        // mp owns the file, reply owns mp
    mp->append(video);

    qInfo() << "Telegram: uploading clip" << path;

    auto* reply = nam_.post(req, mp);
    mp->setParent(reply);

    connect(reply, &QNetworkReply::finished, reply, [reply, path] {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Telegram sendVideo failed:" << reply->errorString();
            qWarning() << "Clip kept at" << path << "(upload failed)";
        } else {
            qInfo() << "Telegram: clip uploaded successfully — deleting" << path;
            QFile::remove(path);
        }
        reply->deleteLater();
    });
}
