#pragma once
#include <QtCore/QObject>
#include <QtNetwork/QTcpServer>

class FrameHub;
class ClipRecorder;

class HttpServer : public QObject {
    Q_OBJECT
public:
    explicit HttpServer(FrameHub*     hub,
                        ClipRecorder* recorder,
                        int           fps    = 10,
                        QObject*      parent = nullptr);

    bool listen(quint16 port);

private:
    FrameHub*     hub_;
    ClipRecorder* recorder_;
    int           fps_;
    QTcpServer    server_;

    void handleConnection();
    void handleRequest(class QTcpSocket* sock, const QByteArray& req);
    void serveIndex(QTcpSocket* sock);
};
