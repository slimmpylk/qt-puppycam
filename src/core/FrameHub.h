#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QReadWriteLock>

class FrameHub {
public:
    void setLatestJpeg(QByteArray jpeg);
    QByteArray latestJpeg() const;

private:
    mutable QReadWriteLock lock_;
    QByteArray latest_;
};
