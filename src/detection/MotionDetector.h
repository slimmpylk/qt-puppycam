#pragma once
#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtGui/QImage>
#include <vector>

class MotionDetector : public QObject {
    Q_OBJECT
public:
    // threshold : number of changed pixels at 320x180 to trigger
    // cooldownSec : minimum seconds between successive triggers
    explicit MotionDetector(int threshold   = 1500,
                            int cooldownSec = 5,
                            QObject* parent = nullptr);

public slots:
    void onNewFrame(const QByteArray& jpeg);

signals:
    void motionDetected(const QByteArray& frame);
    void debugFrame(const QByteArray& grayscaleJpeg);

private:
    std::vector<float> background_; // exponential moving average background model
    int     threshold_;
    int     cooldownMs_;
    qint64  lastDetectMs_ = 0;

    static constexpr int   kW          = 320;
    static constexpr int   kH          = 180;
    static constexpr float kAlpha      = 0.01f; // background update rate (~100 frames to fully adapt)
    static constexpr int   kPixelDiff  = 18;    // luma units to count a pixel as changed
};
