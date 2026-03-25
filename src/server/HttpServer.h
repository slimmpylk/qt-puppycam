#pragma once
#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>

class FrameHub;

class HttpServer : public QObject {
    Q_OBJECT
public:
    // fps is used to drive the MJPEG push interval
    explicit HttpServer(FrameHub* hub, int fps = 10, QObject* parent = nullptr);

    // Binds to 0.0.0.0 (all interfaces) so Tailscale can reach the stream
    bool listen(quint16 port);

private:
    FrameHub*  hub_;
    int        fps_;
    QTcpServer server_;

    void handleConnection();
    void handleRequest(class QTcpSocket* sock, const QByteArray& req);
};
