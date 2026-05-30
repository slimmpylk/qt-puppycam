#include <QtGui/QGuiApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDebug>

#include "core/FrameHub.h"
#include "capture/V4L2MjpegGrabber.h"
#include "server/HttpServer.h"
#include "detection/MotionDetector.h"
#include "detection/AudioMonitor.h"
#include "recording/ClipRecorder.h"
#include "notify/TelegramNotifier.h"

static QString defaultDevice() {
    const QString found = V4L2MjpegGrabber::detectDevice();
    if (!found.isEmpty()) { qInfo() << "Auto-detected camera:" << found; return found; }
    return QStringLiteral("auto");
}

static QString env(const char* key, const QString& def = {}) {
    const QString v = qEnvironmentVariable(key);
    return v.isEmpty() ? def : v;
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName("puppycam");
    QCoreApplication::setApplicationVersion("0.3.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Headless MJPEG webcam streamer with motion/sound detection");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption deviceOpt ({"d","device"}, "V4L2 device or auto.", "path", defaultDevice());
    QCommandLineOption portOpt   ({"p","port"},   "HTTP port.",           "port", env("PUPPYCAM_PORT",   "8080"));
    QCommandLineOption widthOpt  ({"W","width"},  "Capture width px.",    "px",   env("PUPPYCAM_WIDTH",  "1280"));
    QCommandLineOption heightOpt ({"H","height"}, "Capture height px.",   "px",   env("PUPPYCAM_HEIGHT", "720"));
    QCommandLineOption fpsOpt    ({"f","fps"},    "Target fps.",          "fps",  env("PUPPYCAM_FPS",    "10"));

    parser.addOption(deviceOpt); parser.addOption(portOpt);
    parser.addOption(widthOpt);  parser.addOption(heightOpt); parser.addOption(fpsOpt);
    parser.process(app);

    bool ok;
    const QString device = parser.value(deviceOpt);
    const int port   = parser.value(portOpt).toInt(&ok);   if (!ok||port  <1||port>65535) return 2;
    const int width  = parser.value(widthOpt).toInt(&ok);  if (!ok||width <16)            return 2;
    const int height = parser.value(heightOpt).toInt(&ok); if (!ok||height<16)            return 2;
    const int fps    = parser.value(fpsOpt).toInt(&ok);    if (!ok||fps   <1||fps>120)    return 2;

    const int     motionThreshold = env("PUPPYCAM_MOTION_THRESHOLD", "3000").toInt();
    const int     soundThreshold  = env("PUPPYCAM_SOUND_THRESHOLD",  "1500").toInt();
    const QString audioDevice     = env("PUPPYCAM_AUDIO_DEVICE",     "auto");
    const int     preBufferSec    = env("PUPPYCAM_PRE_BUFFER_SEC",   "180").toInt();
    const int     postBufferSec   = env("PUPPYCAM_POST_BUFFER_SEC",  "120").toInt();
    const QString tgToken         = env("PUPPYCAM_TELEGRAM_TOKEN");
    const QString tgChatId        = env("PUPPYCAM_TELEGRAM_CHAT_ID");

    qInfo() << "puppycam v0.3.0 — disarmed by default, arm from the web UI";

    FrameHub hub;

    auto* motion   = new MotionDetector(motionThreshold, 5, &hub);
    auto* audio    = new AudioMonitor(audioDevice, soundThreshold, 5, &hub);
    auto* recorder = new ClipRecorder(preBufferSec, postBufferSec, fps, &hub);
    auto* notifier = new TelegramNotifier(tgToken, tgChatId, &hub);

    // Every frame → motion detector + clip pre-buffer (always, even when disarmed)
    QObject::connect(&hub,    &FrameHub::newFrame,
                     motion,   &MotionDetector::onNewFrame,  Qt::QueuedConnection);
    QObject::connect(&hub,    &FrameHub::newFrame,
                     recorder, &ClipRecorder::onNewFrame,    Qt::QueuedConnection);

    // Detection → trigger (ClipRecorder silently ignores if disarmed)
    QObject::connect(motion, &MotionDetector::motionDetected, recorder,
                     [recorder](const QByteArray&) {
                         recorder->trigger(QStringLiteral("motion"));
                     });
    QObject::connect(audio, &AudioMonitor::soundDetected, recorder,
                     [recorder] {
                         recorder->trigger(QStringLiteral("sound"));
                     });

    // Triggered → send snapshot immediately
    QObject::connect(recorder, &ClipRecorder::triggered,
                     notifier, &TelegramNotifier::onTriggered);

    // Clip ready → upload video, delete local file
    QObject::connect(recorder, &ClipRecorder::clipReady,
                     notifier, &TelegramNotifier::onClipReady);

    // Camera + audio threads
    V4L2MjpegGrabber grabber(&hub, device, width, height, fps);
    grabber.start();
    audio->start();

    // HTTP server — now also takes recorder for arm/disarm API
    HttpServer server(&hub, recorder, fps);
    if (!server.listen(static_cast<quint16>(port))) {
        qCritical() << "Failed to listen on port" << port;
        grabber.stop(); grabber.wait();
        audio->stop();  audio->wait();
        return 1;
    }

    qInfo().nospace() << "Stream ready — http://127.0.0.1:" << port;
    const int rc = app.exec();

    grabber.stop(); grabber.wait();
    audio->stop();  audio->wait();
    return rc;
}
