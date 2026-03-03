#include "FrameHub.h"

void FrameHub::setLatestJpeg(QByteArray jpeg) {
    QWriteLocker g(&lock_);
    latest_ = std::move(jpeg);
}

QByteArray FrameHub::latestJpeg() const {
    QReadLocker g(&lock_);
    return latest_;
}
