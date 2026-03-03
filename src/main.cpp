#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>

#include "core/FrameHub.h"
#include "capture/V4L2MjpegGrabber.h"
#include "server/HttpServer.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    FrameHub hub;

    V4L2MjpegGrabber grabber(
        &hub,
        "/dev/v4l/by-id/usb-EMEET_EMEET_SmartCam_C960_A250926000113128-video-index0",
        1280, 720, 10
        );

    grabber.start(); // <<< FIX: you MUST start the thread

    HttpServer server(&hub);
    if (!server.listenLocalhost(8080)) {
        qCritical() << "Failed to listen on 127.0.0.1:8080";
        grabber.stop();
        grabber.wait();
        return 1;
    }

    qInfo() << "Open http://127.0.0.1:8080 in a browser";
    const int rc = app.exec();

    grabber.stop();
    grabber.wait();
    return rc;
}
