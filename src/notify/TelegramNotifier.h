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
    // Called immediately at trigger — sends snapshot photo right away
    void onTriggered(const QString& reason, const QByteArray& snapshot);

    // Called when mp4 is ready — uploads video then deletes local file
    void onClipReady(const QString& mp4Path, const QString& reason);

private:
    void sendMessage(const QString& text);
    void sendPhoto(const QByteArray& jpeg, const QString& caption);
    void sendVideoAndDelete(const QString& path, const QString& caption);

    QString               token_;
    QString               chatId_;
    QNetworkAccessManager nam_;
};
