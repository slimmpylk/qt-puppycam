#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>

#include "core/FrameHub.h"
#include "capture/V4L2MjpegGrabber.h"
#include "server/HttpServer.h"

// Tries to find a real MJPEG camera at startup so the CLI --help shows a
// sensible default.  If nothing is plugged in yet we return the magic token
// "auto" — the grabber will keep scanning until a camera appears.
static QString defaultDevice()
{
    const QString found = V4L2MjpegGrabber::detectDevice();
    if (!found.isEmpty()) {
        qInfo() << "Auto-detected camera:" << found;
        return found;
    }
    qInfo() << "No MJPEG camera found at startup — will retry when one is plugged in.";
    return QStringLiteral("auto");
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("puppycam");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Headless MJPEG webcam streamer (Qt6 + V4L2)\n"
        "  --device auto        pick first MJPEG-capable camera (default)\n"
        "  --device /dev/video0 pin a specific device"
        );
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption deviceOpt(
        {"d", "device"},
        "V4L2 device path, or \"auto\" to detect automatically.",
        "path",
        defaultDevice()
        );
    QCommandLineOption portOpt(
        {"p", "port"},
        "HTTP listen port (binds 0.0.0.0 so Tailscale can reach it).",
        "port",
        "8080"
        );
    QCommandLineOption widthOpt ({"W", "width"},  "Capture width.",  "px",  "1280");
    QCommandLineOption heightOpt({"H", "height"}, "Capture height.", "px",  "720");
    QCommandLineOption fpsOpt   ({"f", "fps"},    "Target fps.",     "fps", "10");

    parser.addOption(deviceOpt);
    parser.addOption(portOpt);
    parser.addOption(widthOpt);
    parser.addOption(heightOpt);
    parser.addOption(fpsOpt);
    parser.process(app);

    const QString device = parser.value(deviceOpt);

    bool ok = false;
    const int port = parser.value(portOpt).toInt(&ok);
    if (!ok || port < 1 || port > 65535) { qCritical() << "Invalid --port:"   << parser.value(portOpt);   return 2; }
    const int width  = parser.value(widthOpt).toInt(&ok);
    if (!ok || width  < 16 || width  > 7680) { qCritical() << "Invalid --width:"  << parser.value(widthOpt);  return 2; }
    const int height = parser.value(heightOpt).toInt(&ok);
    if (!ok || height < 16 || height > 4320) { qCritical() << "Invalid --height:" << parser.value(heightOpt); return 2; }
    const int fps    = parser.value(fpsOpt).toInt(&ok);
    if (!ok || fps < 1 || fps > 120)         { qCritical() << "Invalid --fps:"    << parser.value(fpsOpt);    return 2; }

    qInfo() << "Starting puppycam:"
            << "device=" << device
            << "size="   << width << "x" << height
            << "fps="    << fps
            << "port="   << port;

    FrameHub hub;

    V4L2MjpegGrabber grabber(&hub, device, width, height, fps);
    grabber.start();

    // Pass fps so HttpServer drives the MJPEG timer at the right rate
    HttpServer server(&hub, fps);
    if (!server.listen(static_cast<quint16>(port))) {
        qCritical() << "Failed to listen on port" << port;
        grabber.stop();
        grabber.wait();
        return 1;
    }

    qInfo() << "Stream ready — open http://<device-ip>:" << port;
    const int rc = app.exec();

    grabber.stop();
    grabber.wait();
    return rc;
}
