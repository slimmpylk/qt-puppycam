#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtNetwork/QTcpServer>
#include <QtNetwork/QTcpSocket>

static QByteArray httpResponse(const QByteArray& body)
{
    QByteArray resp;
    resp += "HTTP/1.1 200 OK\r\n";
    resp += "Content-Type: text/plain; charset=utf-8\r\n";
    resp += "Cache-Control: no-store\r\n";
    resp += "Connection: close\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    resp += "\r\n";
    resp += body;
    return resp;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QTcpServer server;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        while (auto *sock = server.nextPendingConnection()) {
            QObject::connect(sock, &QTcpSocket::readyRead, [sock] {
                sock->readAll(); // ignore request for now
                const auto body = QByteArray("puppycam ok @ ")
                    + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8()
                    + "\n";
                sock->write(httpResponse(body));
                sock->disconnectFromHost();
            });
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
        }
    });

    // Secure default: bind only to localhost
    if (!server.listen(QHostAddress::LocalHost, 8080)) {
        qCritical("Failed to listen on 127.0.0.1:8080");
        return 1;
    }

    qInfo("Listening on http://127.0.0.1:8080");
    return app.exec();
}
