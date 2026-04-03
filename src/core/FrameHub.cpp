#include "FrameHub.h"

FrameHub::FrameHub(QObject* parent) : QObject(parent) {}

void FrameHub::setLatestJpeg(QByteArray jpeg) {
    {
        QWriteLocker g(&lock_);
        latest_ = jpeg;
    }
    // Emit outside the lock — queued connections deliver to the main thread
    emit newFrame(jpeg);
}

QByteArray FrameHub::latestJpeg() const {
    QReadLocker g(&lock_);
    return latest_;
}
