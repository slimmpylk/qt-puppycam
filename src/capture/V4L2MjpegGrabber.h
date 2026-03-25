#pragma once
#include <QtCore/QAtomicInt>
#include <QtCore/QThread>
#include <QtCore/QString>

class FrameHub;

class V4L2MjpegGrabber : public QThread {
    Q_OBJECT
public:
    explicit V4L2MjpegGrabber(FrameHub*      hub,
                              const QString& device = QStringLiteral("auto"),
                              int width  = 1280,
                              int height = 720,
                              int fps    = 10,
                              QObject*   parent = nullptr);

    void stop();

    // Scans /dev/v4l/by-id/ then /dev/video0..7 for the first MJPEG-capable
    // device.  Returns an empty string if nothing is found yet.
    static QString detectDevice();

protected:
    void run() override;

private:
    // One full capture session on resolvedDevice.
    // Returns true  = stop_ was set (clean exit, do not retry).
    // Returns false = error occurred (caller should retry after a delay).
    bool runOnce(const QString& resolvedDevice);
    void sleepMs(int ms);   // ← this line was missing

    FrameHub*  hub_;
    QString    dev_;
    int        width_;
    int        height_;
    int        fps_;
    QAtomicInt stop_{0};

    static constexpr int kRetryMs = 3000;
};
