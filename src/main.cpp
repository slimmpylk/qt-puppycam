#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>

#include "core/FrameHub.h"
#include "capture/V4L2MjpegGrabber.h"
#include "server/HttpServer.h"

static QString defaultDevice()
{
    // Your current camera. You can change this default later, but CLI can always override it.
    return "/dev/v4l/by-id/usb-EMEET_EMEET_SmartCam_C960_A250926000113128-video-index0";
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("puppycam");
    QCoreApplication::setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Headless MJPEG webcam streamer (Qt6 + V4L2)");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption deviceOpt(
        {"d", "device"},
        "V4L2 device path (recommended: /dev/v4l/by-id/...-video-index0).",
        "path",
        defaultDevice()
        );
    QCommandLineOption portOpt(
        {"p", "port"},
        "HTTP listen port (binds to 127.0.0.1).",
        "port",
        "8080"
        );
    QCommandLineOption widthOpt({"W", "width"}, "Capture width.", "px", "1280");
    QCommandLineOption heightOpt({"H", "height"}, "Capture height.", "px", "720");
    QCommandLineOption fpsOpt({"f", "fps"}, "Target capture fps (best-effort).", "fps", "10");

    parser.addOption(deviceOpt);
    parser.addOption(portOpt);
    parser.addOption(widthOpt);
    parser.addOption(heightOpt);
    parser.addOption(fpsOpt);

    parser.process(app);

    const QString device = parser.value(deviceOpt);

    bool ok = false;
    const int port = parser.value(portOpt).toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        qCritical() << "Invalid --port:" << parser.value(portOpt);
        return 2;
    }

    const int width = parser.value(widthOpt).toInt(&ok);
    if (!ok || width < 16 || width > 7680) {
        qCritical() << "Invalid --width:" << parser.value(widthOpt);
        return 2;
    }

    const int height = parser.value(heightOpt).toInt(&ok);
    if (!ok || height < 16 || height > 4320) {
        qCritical() << "Invalid --height:" << parser.value(heightOpt);
        return 2;
    }

    const int fps = parser.value(fpsOpt).toInt(&ok);
    if (!ok || fps < 1 || fps > 120) {
        qCritical() << "Invalid --fps:" << parser.value(fpsOpt);
        return 2;
    }

    qInfo() << "Starting puppycam with:"
            << "device=" << device
            << "size=" << width << "x" << height
            << "fps=" << fps
            << "port=" << port;

    FrameHub hub;

    V4L2MjpegGrabber grabber(&hub, device, width, height, fps);
    grabber.start();

    HttpServer server(&hub);
    if (!server.listenLocalhost(static_cast<quint16>(port))) {
        qCritical() << "Failed to listen on 127.0.0.1:" << port;
        grabber.stop();
        grabber.wait();
        return 1;
    }

    qInfo() << "Open http://127.0.0.1:" << port << "in a browser";
    const int rc = app.exec();

    grabber.stop();
    grabber.wait();
    return rc;
}
