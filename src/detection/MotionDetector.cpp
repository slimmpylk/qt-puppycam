#include "MotionDetector.h"
#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <cstdlib>

MotionDetector::MotionDetector(int threshold, int cooldownSec, QObject* parent)
    : QObject(parent), threshold_(threshold), cooldownMs_(cooldownSec * 1000) {}

void MotionDetector::onNewFrame(const QByteArray& jpeg)
{
    // Decode, shrink, convert to grayscale — cheap on Pi 5
    const QImage cur = QImage::fromData(jpeg, "JPEG")
                           .scaled(kW, kH, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                           .convertToFormat(QImage::Format_Grayscale8);
    if (cur.isNull()) return;

    // Emit debug stream (grayscale 320x180) for inspection
    {
        QByteArray dbg;
        QBuffer buf(&dbg);
        buf.open(QIODevice::WriteOnly);
        cur.save(&buf, "JPEG", 70);
        emit debugFrame(dbg);
    }

    if (prev_.isNull()) { prev_ = cur; return; }

    // Count pixels that changed by more than 25 luma units
    int changed = 0;
    const uchar* a = prev_.constBits();
    const uchar* b = cur.constBits();
    for (int i = 0; i < kW * kH; ++i)
        if (std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i])) > 25)
            ++changed;

    prev_ = cur;

    if (changed >= threshold_) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastDetectMs_ > cooldownMs_) {
            lastDetectMs_ = now;
            qInfo() << "Motion detected:" << changed << "pixels changed";
            emit motionDetected(jpeg);
        }
    }
}
