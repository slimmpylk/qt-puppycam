#include "HttpServer.h"
#include "../core/FrameHub.h"

#include <QtCore/QTimer>
#include <QtNetwork/QTcpSocket>
#include <QtNetwork/QHostAddress>

// ── small HTTP response builders ──────────────────────────────────────────────

static QByteArray httpText(int code,
                           const QByteArray& body,
                           const QByteArray& ctype = "text/html; charset=utf-8")
{
    QByteArray r;
    r += "HTTP/1.1 " + QByteArray::number(code) + " OK\r\n";
    r += "Content-Type: "   + ctype + "\r\n";
    r += "Cache-Control: no-store\r\n";
    r += "Connection: close\r\n";
    r += "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n";
    r += body;
    return r;
}

static QByteArray httpJpeg(const QByteArray& jpeg)
{
    QByteArray r;
    r += "HTTP/1.1 200 OK\r\n";
    r += "Content-Type: image/jpeg\r\n";
    r += "Cache-Control: no-store\r\n";
    r += "Connection: close\r\n";
    r += "Content-Length: " + QByteArray::number(jpeg.size()) + "\r\n\r\n";
    r += jpeg;
    return r;
}

// ── path parser ───────────────────────────────────────────────────────────────

static QByteArray parsePath(const QByteArray& req)
{
    int eol = req.indexOf("\r\n");
    QByteArray line = (eol >= 0) ? req.left(eol) : req;
    if (!line.startsWith("GET ")) return "/";
    int sp1 = 4;
    int sp2 = line.indexOf(' ', sp1);
    return (sp2 < 0) ? QByteArray("/") : line.mid(sp1, sp2 - sp1);
}

// ── constructor / listen ──────────────────────────────────────────────────────

HttpServer::HttpServer(FrameHub* hub, int fps, QObject* parent)
    : QObject(parent), hub_(hub), fps_(fps)
{
    connect(&server_, &QTcpServer::newConnection,
            this,     &HttpServer::handleConnection);
}

bool HttpServer::listen(quint16 port)
{
    // Bind to every interface (0.0.0.0) — required for Tailscale
    return server_.listen(QHostAddress::Any, port);
}

// ── connection handling ───────────────────────────────────────────────────────

void HttpServer::handleConnection()
{
    while (auto* sock = server_.nextPendingConnection()) {
        sock->setParent(this);
        connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
            handleRequest(sock, sock->readAll());
        });
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void HttpServer::handleRequest(QTcpSocket* sock, const QByteArray& req)
{
    const QByteArray path = parsePath(req);

    // ── / index page ──────────────────────────────────────────────────────────
    if (path == "/" || path == "/index.html") {
        const QByteArray html =
            "<!doctype html><html><head>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>puppycam</title>"
            "<style>"
            "body{font-family:sans-serif;margin:12px;background:#111;color:#eee}"
            "#wrap{max-width:980px;margin:0 auto}"
            "img{width:100%;height:auto;border-radius:10px;background:#000}"
            "button,a.btn{padding:10px 16px;font-size:15px;border-radius:8px;"
            "  border:0;background:#3a86ff;color:#fff;text-decoration:none;cursor:pointer}"
            ".row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:10px 0}"
            "</style></head><body><div id='wrap'>"
            "<h3>🐾 puppycam</h3>"
            "<div class='row'>"
            "  <button id='fs'>⛶ Fullscreen</button>"
            "  <a class='btn' href='/snapshot.jpg'>📷 snapshot.jpg</a>"
            "</div>"
            "<img id='cam' src='/mjpeg' alt='live stream' />"
            "<script>"
            "const cam=document.getElementById('cam');"
            "const goFs=()=>{"
            "  if(cam.requestFullscreen)cam.requestFullscreen();"
            "  else if(cam.webkitRequestFullscreen)cam.webkitRequestFullscreen();"
            "};"
            "document.getElementById('fs').addEventListener('click',goFs);"
            "cam.addEventListener('click',goFs);"
            // Reconnect the stream automatically if the img src breaks
            "cam.addEventListener('error',()=>{"
            "  setTimeout(()=>{cam.src='/mjpeg?t='+Date.now();},2000);"
            "});"
            "</script>"
            "</div></body></html>";
        sock->write(httpText(200, html));
        sock->disconnectFromHost();
        return;
    }

    // ── /snapshot.jpg ─────────────────────────────────────────────────────────
    if (path == "/snapshot.jpg") {
        const QByteArray jpeg = hub_->latestJpeg();
        if (jpeg.isEmpty())
            sock->write(httpText(503, "No frame yet — camera may still be initialising.",
                                 "text/plain; charset=utf-8"));
        else
            sock->write(httpJpeg(jpeg));
        sock->disconnectFromHost();
        return;
    }

    // ── /mjpeg  multipart stream ───────────────────────────────────────────────
    if (path.startsWith("/mjpeg")) {
        sock->write(
            "HTTP/1.1 200 OK\r\n"
            "Connection: close\r\n"
            "Cache-Control: no-store\r\n"
            "Pragma: no-cache\r\n"
            "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n"
            );

        const int intervalMs = 1000 / qMax(1, fps_);
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
        timer->start(intervalMs);
        return;
    }

    // ── 404 ───────────────────────────────────────────────────────────────────
    sock->write(httpText(404, "Not found.", "text/plain; charset=utf-8"));
    sock->disconnectFromHost();
}
