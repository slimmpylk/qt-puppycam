#pragma once
#include <QtCore/QAtomicInt>
#include <QtCore/QThread>
#include <QtCore/QString>

class AudioMonitor : public QThread {
    Q_OBJECT
public:
    // device     : ALSA device string ("auto", "hw:1,0", "default", ...)
    // threshold  : RMS amplitude to trigger (0–32767)
    // cooldownSec: minimum seconds between triggers
    explicit AudioMonitor(const QString& device     = QStringLiteral("auto"),
                          int            threshold  = 1500,
                          int            cooldownSec = 5,
                          QObject*       parent     = nullptr);

    void stop();

    // Scans /proc/asound/cards for the first USB audio device.
    // Returns "default" if nothing is found.
    static QString detectAudioDevice();

signals:
    void soundDetected();

protected:
    void run() override;

private:
    QString    device_;
    int        threshold_;
    int        cooldownMs_;
    QAtomicInt stop_{0};
};
