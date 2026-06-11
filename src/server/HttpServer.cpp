#include "HttpServer.h"
#include "../core/FrameHub.h"
#include "../recording/ClipRecorder.h"

#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>

// ── response helpers ──────────────────────────────────────────────────────────

static QByteArray httpText(int code, const QByteArray& body,
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

static QByteArray httpJson(const QByteArray& json)
{
    return httpText(200, json, "application/json");
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

static QByteArray parsePath(const QByteArray& req)
{
    int eol = req.indexOf("\r\n");
    QByteArray line = (eol >= 0) ? req.left(eol) : req;
    if (!line.startsWith("GET ") && !line.startsWith("POST ")) return "/";
    int sp1 = line.indexOf(' ') + 1;
    int sp2 = line.indexOf(' ', sp1);
    return (sp2 < 0) ? QByteArray("/") : line.mid(sp1, sp2 - sp1);
}

static bool isPost(const QByteArray& req)
{
    return req.startsWith("POST ");
}

// ── constructor ───────────────────────────────────────────────────────────────

HttpServer::HttpServer(FrameHub* hub, ClipRecorder* recorder, int fps, FrameHub* debugHub, QObject* parent)
    : QObject(parent), hub_(hub), debugHub_(debugHub), recorder_(recorder), fps_(fps)
{
    connect(&server_, &QTcpServer::newConnection,
            this,     &HttpServer::handleConnection);
}

bool HttpServer::listen(quint16 port)
{
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

// ── request routing ───────────────────────────────────────────────────────────

void HttpServer::handleRequest(QTcpSocket* sock, const QByteArray& req)
{
    const QByteArray path = parsePath(req);

    // ── API: arm / disarm / status ────────────────────────────────────────────
    if (path == "/api/arm" && isPost(req)) {
        recorder_->arm();
        sock->write(httpJson("{\"armed\":true}"));
        sock->disconnectFromHost();
        return;
    }
    if (path == "/api/disarm" && isPost(req)) {
        recorder_->disarm();
        sock->write(httpJson("{\"armed\":false}"));
        sock->disconnectFromHost();
        return;
    }
    if (path == "/api/status") {
        const QByteArray json = recorder_->isArmed()
        ? "{\"armed\":true}"
        : "{\"armed\":false}";
        sock->write(httpJson(json));
        sock->disconnectFromHost();
        return;
    }

    // ── snapshot ──────────────────────────────────────────────────────────────
    if (path == "/snapshot.jpg") {
        const QByteArray jpeg = hub_->latestJpeg();
        if (jpeg.isEmpty())
            sock->write(httpText(503, "No frame yet.", "text/plain; charset=utf-8"));
        else
            sock->write(httpJpeg(jpeg));
        sock->disconnectFromHost();
        return;
    }

    // ── MJPEG stream (HD) ─────────────────────────────────────────────────────
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
            chunk += jpeg + "\r\n";
            sock->write(chunk);
            sock->flush();
        });
        timer->start(intervalMs);
        return;
    }

    // ── MJPEG debug stream (320x180 grayscale — what motion detector sees) ────
    if (path.startsWith("/debug-mjpeg") && debugHub_) {
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
            const QByteArray jpeg = debugHub_->latestJpeg();
            if (jpeg.isEmpty()) return;
            QByteArray chunk;
            chunk += "--frame\r\n";
            chunk += "Content-Type: image/jpeg\r\n";
            chunk += "Content-Length: " + QByteArray::number(jpeg.size()) + "\r\n\r\n";
            chunk += jpeg + "\r\n";
            sock->write(chunk);
            sock->flush();
        });
        timer->start(intervalMs);
        return;
    }

    // ── debug page ────────────────────────────────────────────────────────────
    if (path == "/debug") {
        const QByteArray html = R"HTML(
<!doctype html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>puppycam — motion debug</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:sans-serif;background:#111;color:#eee;padding:12px}
  h3{margin:10px 0 4px;font-size:16px;font-weight:500}
  p{font-size:12px;color:#888;margin-bottom:12px}
  img{width:320px;height:180px;border-radius:6px;background:#000;display:block;image-rendering:pixelated}
  a{color:#7af;font-size:14px;display:inline-block;margin-top:12px}
</style>
</head><body>
<h3>Motion detector view — 320x180 grayscale</h3>
<p>This is exactly what the motion detector compares frame-to-frame. Pixels must differ by &gt;25 luma units to count as changed.</p>
<img src='/debug-mjpeg' alt='debug stream' />
<br><a href='/'>&#8592; Back to main camera</a>
</body></html>
)HTML";
        sock->write(httpText(200, html));
        sock->disconnectFromHost();
        return;
    }

    // ── index page ────────────────────────────────────────────────────────────
    if (path == "/" || path == "/index.html") {
        serveIndex(sock);
        return;
    }

    sock->write(httpText(404, "Not found.", "text/plain; charset=utf-8"));
    sock->disconnectFromHost();
}

// ── HTML page ─────────────────────────────────────────────────────────────────

void HttpServer::serveIndex(QTcpSocket* sock)
{
    const QByteArray html = R"HTML(
<!doctype html><html><head>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>puppycam</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:sans-serif;background:#111;color:#eee;padding:12px}
  #wrap{max-width:980px;margin:0 auto}
  h3{margin:10px 0 12px;font-size:18px;font-weight:500}
  img{width:100%;height:auto;border-radius:10px;background:#000;display:block}
  .row{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin:10px 0}
  button{padding:10px 18px;font-size:15px;border-radius:8px;border:0;cursor:pointer;font-weight:500}
  #btn-arm{background:#2ecc71;color:#fff;min-width:140px;transition:background .3s}
  #btn-arm.disarmed{background:#555;color:#ccc}
  #status{font-size:13px;color:#aaa}
  .dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;background:#555}
  .dot.armed{background:#2ecc71;box-shadow:0 0 6px #2ecc71}
  a.btn{padding:10px 16px;font-size:15px;border-radius:8px;background:#333;
        color:#eee;text-decoration:none;cursor:pointer}
</style>
</head><body><div id='wrap'>
<h3>🐾 puppycam</h3>
<div class='row'>
  <button id='btn-arm' class='disarmed' onclick='toggleArm()'>🔒 Arm</button>
  <span id='status'><span class='dot' id='dot'></span><span id='status-text'>Disarmed</span></span>
  <a class='btn' href='/snapshot.jpg'>📷 Snapshot</a>
</div>
<img id='cam' src='/mjpeg' alt='live stream' />
<script>
let armed = false;

async function fetchStatus() {
  try {
    const r = await fetch('/api/status');
    const d = await r.json();
    setArmed(d.armed);
  } catch(e) {}
}

function setArmed(a) {
  armed = a;
  const btn  = document.getElementById('btn-arm');
  const dot  = document.getElementById('dot');
  const txt  = document.getElementById('status-text');
  if (a) {
    btn.textContent = '🟢 Disarm';
    btn.classList.remove('disarmed');
    dot.classList.add('armed');
    txt.textContent = 'Armed — recording on motion/sound';
  } else {
    btn.textContent = '🔒 Arm';
    btn.classList.add('disarmed');
    dot.classList.remove('armed');
    txt.textContent = 'Disarmed';
  }
}

async function toggleArm() {
  const endpoint = armed ? '/api/disarm' : '/api/arm';
  try {
    const r = await fetch(endpoint, {method:'POST'});
    const d = await r.json();
    setArmed(d.armed);
  } catch(e) {}
}

// Reconnect stream if it drops
const cam = document.getElementById('cam');
cam.addEventListener('error', () => {
  setTimeout(() => { cam.src = '/mjpeg?t=' + Date.now(); }, 2000);
});
cam.addEventListener('click', () => {
  if (cam.requestFullscreen) cam.requestFullscreen();
  else if (cam.webkitRequestFullscreen) cam.webkitRequestFullscreen();
});

// Poll status every 3 seconds (in case another device changed it)
fetchStatus();
setInterval(fetchStatus, 3000);
</script>
</div></body></html>
)HTML";

    sock->write(httpText(200, html));
    sock->disconnectFromHost();
}
