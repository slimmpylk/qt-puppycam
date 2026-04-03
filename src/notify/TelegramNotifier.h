#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtNetwork/QNetworkAccessManager>

class TelegramNotifier : public QObject {
    Q_OBJECT
public:
    explicit TelegramNotifier(const QString& token,
                              const QString& chatId,
                              QObject*       parent = nullptr);

    bool isConfigured() const;

public slots:
    void onClipReady(const QString& mp4Path,
                     const QString& reason,
                     const QByteArray& snapshot);

private:
    void sendMessage(const QString& text);
    void sendPhoto(const QByteArray& jpeg, const QString& caption);

    // Uploads mp4 to Telegram, deletes local file on success
    void sendVideoAndDelete(const QString& path, const QString& caption);

    QString               token_;
    QString               chatId_;
    QNetworkAccessManager nam_;
};
