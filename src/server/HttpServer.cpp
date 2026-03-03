#include "HttpServer.h"
#include "../core/FrameHub.h"

#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtNetwork/QTcpSocket>

static QByteArray httpText(int code, const QByteArray& body, const QByteArray& ctype="text/html; charset=utf-8") {
    QByteArray resp;
    resp += "HTTP/1.1 " + QByteArray::number(code) + " OK\r\n";
    resp += "Content-Type: " + ctype + "\r\n";
    resp += "Cache-Control: no-store\r\n";
    resp += "Connection: close\r\n";
    resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    resp += body;
    return resp;
}

static QByteArray httpJpeg(const QByteArray& jpeg) {
    QByteArray resp;
    resp += "HTTP/1.1 200 OK\r\n";
    resp += "Content-Type: image/jpeg\r\n";
    resp += "Cache-Control: no-store\r\n";
    resp += "Connection: close\r\n";
    resp += "Content-Length: " + QByteArray::number(jpeg.size()) + "\r\n\r\n";
    resp += jpeg;
    return resp;
}

HttpServer::HttpServer(FrameHub* hub, QObject* parent)
    : QObject(parent), hub_(hub) {
    connect(&server_, &QTcpServer::newConnection, this, &HttpServer::handleConnection);
}

bool HttpServer::listenLocalhost(quint16 port) {
    return server_.listen(QHostAddress::LocalHost, port);
}

void HttpServer::handleConnection() {
    while (auto* sock = server_.nextPendingConnection()) {
        sock->setParent(this);

        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            const QByteArray req = sock->readAll();
            handleRequest(sock, req);
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

// Very small HTTP GET parser: extracts path from "GET /path HTTP/1.1"
static QByteArray parsePath(const QByteArray& req) {
    int eol = req.indexOf("\r\n");
    QByteArray line = (eol >= 0) ? req.left(eol) : req;
    // Expect: GET <path> HTTP/1.1
    if (!line.startsWith("GET ")) return "/";
    int sp1 = 3;
    while (sp1 < line.size() && line[sp1] == ' ') sp1++;
    int sp2 = line.indexOf(' ', sp1);
    if (sp2 < 0) return "/";
    return line.mid(sp1, sp2 - sp1);
}

void HttpServer::handleRequest(QTcpSocket* sock, const QByteArray& req) {
    const QByteArray path = parsePath(req);

    if (path == "/" || path == "/index.html") {
        const QByteArray html =
            "<!doctype html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
            "<title>puppycam</title></head><body>"
            "<h3>puppycam</h3>"
            "<img src='/mjpeg' style='max-width:100%;height:auto;' />"
            "<p><a href='/snapshot.jpg'>snapshot.jpg</a></p>"
            "</body></html>";
        sock->write(httpText(200, html));
        sock->disconnectFromHost();
        return;
    }

    if (path == "/snapshot.jpg") {
        const QByteArray jpeg = hub_->latestJpeg();
        if (jpeg.isEmpty()) {
            sock->write(httpText(503, "No frame yet\n", "text/plain; charset=utf-8"));
        } else {
            sock->write(httpJpeg(jpeg));
        }
        sock->disconnectFromHost();
        return;
    }

    if (path == "/mjpeg") {
        // Keep the socket open and stream multipart MJPEG
        sock->write(
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            "Cache-Control: no-store\r\n"
            "Pragma: no-cache\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n"
            );

        // Stream timer: send latest frame periodically (simple, reliable)
        auto* timer = new QTimer(sock);
        connect(timer, &QTimer::timeout, sock, [this, sock] {
            if (!sock->isOpen()) return;

            const QByteArray jpeg = hub_->latestJpeg();
            if (jpeg.isEmpty()) return;

            QByteArray chunk;
            chunk += "--frame\r\n";
            chunk += "Content-Type: image/jpeg\r\n";
            chunk += "Content-Length: " + QByteArray::number(jpeg.size()) + "\r\n\r\n";
            chunk += jpeg;
            chunk += "\r\n";
            sock->write(chunk);
            sock->flush();
        });

        timer->start(100); // ~10 fps (we'll make configurable later)
        return;
    }

    sock->write(httpText(404, "Not found\n", "text/plain; charset=utf-8"));
    sock->disconnectFromHost();
}
