#include "MotionDetector.h"
#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <cstdlib>

MotionDetector::MotionDetector(int threshold, int cooldownSec, QObject* parent)
    : QObject(parent), threshold_(threshold), cooldownMs_(cooldownSec * 1000) {}

void MotionDetector::onNewFrame(const QByteArray& jpeg)
{
    const QImage cur = QImage::fromData(jpeg, "JPEG")
                           .scaled(kW, kH, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                           .convertToFormat(QImage::Format_Grayscale8);
    if (cur.isNull()) return;

    // Emit debug stream so the detector's view is visible at /debug
    {
        QByteArray dbg;
        QBuffer buf(&dbg);
        buf.open(QIODevice::WriteOnly);
        cur.save(&buf, "JPEG", 70);
        emit debugFrame(dbg);
    }

    const uchar* px = cur.constBits();

    // First frame — seed the background model and return
    if (background_.empty()) {
        background_.resize(kW * kH);
        for (int i = 0; i < kW * kH; ++i)
            background_[i] = static_cast<float>(px[i]);
        return;
    }

    // Count pixels that differ from the background model
    int changed = 0;
    for (int i = 0; i < kW * kH; ++i)
        if (std::abs(static_cast<int>(px[i]) - static_cast<int>(background_[i])) > kPixelDiff)
            ++changed;

    // Always update background slowly so it adapts to gradual scene changes
    // (e.g. lighting shifts, dog settling into a new sleeping position)
    for (int i = 0; i < kW * kH; ++i)
        background_[i] = background_[i] * (1.0f - kAlpha) + px[i] * kAlpha;

    if (changed >= threshold_) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastDetectMs_ > cooldownMs_) {
            lastDetectMs_ = now;
            qInfo() << "Motion detected:" << changed << "pixels changed";
            emit motionDetected(jpeg);
        }
    }
}
