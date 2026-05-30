#include "TelegramNotifier.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtNetwork/QHttpMultiPart>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

TelegramNotifier::TelegramNotifier(const QString& token,
                                   const QString& chatId,
                                   QObject*       parent)
    : QObject(parent), token_(token), chatId_(chatId)
{
    connect(&pollTimer_, &QTimer::timeout, this, &TelegramNotifier::pollUpdates);
    pollTimer_.start(30000);
}

bool TelegramNotifier::isConfigured() const {
    return !token_.isEmpty() && !chatId_.isEmpty();
}

// ── immediate snapshot — fired right at trigger moment ────────────────────────

void TelegramNotifier::onTriggered(const QString& reason, const QByteArray& snapshot)
{
    if (!isConfigured()) {
        qInfo() << "TelegramNotifier: not configured — skipping notification";
        return;
    }

    const QString caption =
        QStringLiteral("🐾 puppycam alert!\nTrigger: %1\n📹 Video clip incoming...").arg(reason);

    if (!snapshot.isEmpty())
        sendPhoto(snapshot, caption);
    else
        sendMessage(caption);
}

// ── video upload — fired when mp4 is ready ────────────────────────────────────

void TelegramNotifier::onClipReady(const QString& mp4Path, const QString& reason)
{
    if (!isConfigured()) {
        QFile::remove(mp4Path); // still clean up even if not configured
        return;
    }

    sendVideoAndDelete(mp4Path,
                       QStringLiteral("📹 Full clip — trigger: %1").arg(reason));
}

// ── helpers ───────────────────────────────────────────────────────────────────

void TelegramNotifier::sendMessage(const QString& text)
{
    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.telegram.org/bot%1/sendMessage").arg(token_)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const QString safe = QString(text).replace('\\', "\\\\")
                             .replace('"',  "\\\"")
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
                    QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name)));
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
            qInfo() << "Telegram: snapshot sent immediately";
        reply->deleteLater();
    });
}

void TelegramNotifier::sendVideoAndDelete(const QString& path, const QString& caption)
{
    auto* file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning() << "TelegramNotifier: cannot open" << path;
        delete file;
        return;
    }

    QNetworkRequest req(
        QUrl(QStringLiteral("https://api.telegram.org/bot%1/sendVideo").arg(token_)));

    auto* mp = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addField = [&](const QByteArray& name, const QByteArray& value) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name)));
        p.setBody(value);
        mp->append(p);
    };

    addField("chat_id",             chatId_.toUtf8());
    addField("caption",             caption.toUtf8());
    addField("supports_streaming",  "true");

    QHttpPart video;
    video.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"video\"; filename=\"clip.mp4\""));
    video.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("video/mp4"));
    video.setBodyDevice(file);
    file->setParent(mp);
    mp->append(video);

    qInfo() << "Telegram: uploading clip" << path;

    auto* reply = nam_.post(req, mp);
    mp->setParent(reply);

    connect(reply, &QNetworkReply::finished, reply, [reply, path] {
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Telegram sendVideo failed:" << reply->errorString();
            qWarning() << "Clip kept at" << path << "(upload failed — retrieve via SSH)";
        } else {
            qInfo() << "Telegram: clip uploaded — deleting local file" << path;
            QFile::remove(path);
        }
        reply->deleteLater();
    });
}

void TelegramNotifier::pollUpdates()
{
    if (!isConfigured()) return;

    QUrl url(QStringLiteral("https://api.telegram.org/bot%1/getUpdates?offset=%2&timeout=0")
             .arg(token_).arg(updateOffset_));

    auto* reply = nam_.get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;

        const QJsonArray results =
            QJsonDocument::fromJson(reply->readAll())
                .object().value("result").toArray();

        for (const QJsonValue& v : results) {
            const QJsonObject update = v.toObject();
            const qint64 updateId = update.value("update_id").toInteger();
            updateOffset_ = updateId + 1;

            const QString fromChat =
                QString::number(update.value("message").toObject()
                                      .value("chat").toObject()
                                      .value("id").toInteger());

            if (fromChat != chatId_)
                qInfo() << "TelegramNotifier: ignored message from unauthorized chat" << fromChat;
        }
    });
}
