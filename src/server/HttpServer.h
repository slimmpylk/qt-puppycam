#pragma once
#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>

class FrameHub;

class HttpServer : public QObject {
    Q_OBJECT
public:
    explicit HttpServer(FrameHub* hub, QObject* parent = nullptr);
    bool listenLocalhost(quint16 port);

private:
    FrameHub* hub_;
    QTcpServer server_;

    void handleConnection();
    void handleRequest(class QTcpSocket* sock, const QByteArray& req);
};
