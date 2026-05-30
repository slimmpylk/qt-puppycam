#include "FrameHub.h"
#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtCore/QBuffer>
#include <QtCore/QDateTime>


FrameHub::FrameHub(QObject* parent) : QObject(parent) {}

void FrameHub::setLatestJpeg(QByteArray jpeg) {
    QImage img;
    img.loadFromData(jpeg, "JPEG");

    if (!img.isNull()) {
        QPainter p(&img);
        p.setFont(QFont("Monospace", 18, QFont::Bold));

        QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");

        p.setPen(Qt::black);
        p.drawText(12, 32, timeStr);   // shadow

        p.setPen(Qt::white);
        p.drawText(10, 30, timeStr);   // main text

        p.end();

        QBuffer buf;
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "JPEG", 85);
        jpeg = buf.data();
    }

    {
        QWriteLocker g(&lock_);
        latest_ = jpeg;
    }
    emit newFrame(jpeg);
}


QByteArray FrameHub::latestJpeg() const {
    QReadLocker g(&lock_);
    return latest_;
}
