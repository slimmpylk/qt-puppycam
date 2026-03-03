#pragma once
#include <QtCore/QObject>
#include <QtCore/QAtomicInt>
#include <QtCore/QThread>
#include <QtCore/QString>

class FrameHub;

class V4L2MjpegGrabber : public QThread {
    Q_OBJECT
public:
    V4L2MjpegGrabber(FrameHub* hub,
                     QString device = "/dev/video0",
                     int width = 1280,
                     int height = 720,
                     int fps = 15,
                     QObject* parent = nullptr);

    void stop();

protected:
    void run() override;

private:
    FrameHub* hub_;
    QString dev_;
    int width_;
    int height_;
    int fps_;
    QAtomicInt stop_{0};
};
