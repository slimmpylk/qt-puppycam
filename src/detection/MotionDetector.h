#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QReadWriteLock>

class FrameHub : public QObject {
    Q_OBJECT
public:
    explicit FrameHub(QObject* parent = nullptr);

    void       setLatestJpeg(QByteArray jpeg);
    QByteArray latestJpeg() const;

signals:
    // Emitted for every captured frame — connected via Qt::QueuedConnection
    void newFrame(QByteArray jpeg);

private:
    mutable QReadWriteLock lock_;
    QByteArray             latest_;
};
