#pragma once
#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtGui/QImage>

class MotionDetector : public QObject {
    Q_OBJECT
public:
    // threshold : number of changed pixels at 320x180 grayscale to trigger
    // cooldownSec : minimum seconds between successive triggers
    explicit MotionDetector(int threshold   = 3000,
                            int cooldownSec = 5,
                            QObject* parent = nullptr);

public slots:
    void onNewFrame(const QByteArray& jpeg);

signals:
    void motionDetected(const QByteArray& frame);
    void debugFrame(const QByteArray& grayscaleJpeg);

private:
    QImage  prev_;
    int     threshold_;
    int     cooldownMs_;
    qint64  lastDetectMs_ = 0;

    static constexpr int kW = 320;
    static constexpr int kH = 180;
};
