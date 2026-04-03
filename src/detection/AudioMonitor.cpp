#include "AudioMonitor.h"

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QFile>

#include <alsa/asoundlib.h>
#include <cmath>
#include <vector>

// ── device auto-detection ─────────────────────────────────────────────────────

QString AudioMonitor::detectAudioDevice()
{
    QFile f(QStringLiteral("/proc/asound/cards"));
    if (f.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
        for (const QString& line : lines) {
            if (!line.contains(QStringLiteral("USB"), Qt::CaseInsensitive)) continue;
            for (const QChar c : line) {
                if (c.isDigit()) {
                    const QString dev = QStringLiteral("hw:%1,0").arg(c.digitValue());
                    qInfo() << "AudioMonitor: auto-detected" << dev;
                    return dev;
                }
            }
        }
    }
    qInfo() << "AudioMonitor: falling back to ALSA 'default'";
    return QStringLiteral("default");
}

// ── constructor / stop ────────────────────────────────────────────────────────

AudioMonitor::AudioMonitor(const QString& device, int threshold, int cooldownSec, QObject* parent)
    : QThread(parent)
    , device_(device)
    , threshold_(threshold)
    , cooldownMs_(cooldownSec * 1000)
{}

void AudioMonitor::stop() { stop_.storeRelease(1); }

// ── capture thread ────────────────────────────────────────────────────────────

void AudioMonitor::run()
{
    const QString resolved =
        (device_.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
            ? detectAudioDevice()
            : device_;

    snd_pcm_t*          handle = nullptr;
    snd_pcm_hw_params_t* params = nullptr;

    int err = snd_pcm_open(&handle,
                           resolved.toLocal8Bit().constData(),
                           SND_PCM_STREAM_CAPTURE, 0);
    if (err < 0) {
        qWarning() << "AudioMonitor: cannot open" << resolved << "—" << snd_strerror(err);
        return;
    }

    snd_pcm_hw_params_malloc(&params);
    snd_pcm_hw_params_any(handle, params);
    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(handle, params, 1);
    unsigned int rate = 16000;
    snd_pcm_hw_params_set_rate_near(handle, params, &rate, nullptr);
    snd_pcm_uframes_t period = 1024;
    snd_pcm_hw_params_set_period_size_near(handle, params, &period, nullptr);

    if ((err = snd_pcm_hw_params(handle, params)) < 0) {
        qWarning() << "AudioMonitor: hw_params failed:" << snd_strerror(err);
        snd_pcm_hw_params_free(params);
        snd_pcm_close(handle);
        return;
    }
    snd_pcm_hw_params_free(params);
    snd_pcm_prepare(handle);

    qInfo() << "AudioMonitor: listening on" << resolved << "at" << rate << "Hz, threshold" << threshold_;

    std::vector<int16_t> buf(period);
    qint64 lastDetectMs = 0;

    while (!stop_.loadAcquire()) {
        int n = snd_pcm_readi(handle, buf.data(), period);
        if (n == -EPIPE) { snd_pcm_prepare(handle); continue; }
        if (n < 0) { qWarning() << "AudioMonitor: read error:" << snd_strerror(n); break; }

        // RMS amplitude
        double sum = 0;
        for (int i = 0; i < n; ++i)
            sum += static_cast<double>(buf[i]) * buf[i];
        const int rms = static_cast<int>(std::sqrt(sum / n));

        if (rms >= threshold_) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - lastDetectMs > cooldownMs_) {
                lastDetectMs = now;
                qInfo() << "Sound detected: RMS =" << rms;
                emit soundDetected();
            }
        }
    }

    snd_pcm_close(handle);
}
