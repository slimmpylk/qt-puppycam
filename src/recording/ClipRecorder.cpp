#include "ClipRecorder.h"

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>

static const QString kClipDir = QStringLiteral("/var/lib/puppycam/clips");

ClipRecorder::ClipRecorder(int preBufferSec, int postBufferSec,
                           int fps, QObject* parent)
    : QObject(parent)
    , fps_(fps)
    , maxPreFrames_(preBufferSec * fps)
    , postFrames_(postBufferSec * fps)
{}

// ── arm / disarm ──────────────────────────────────────────────────────────────

void ClipRecorder::arm()
{
    if (armed_) return;
    armed_ = true;
    qInfo() << "ClipRecorder: ARMED — motion/sound detection active";
    emit armedChanged(true);
}

void ClipRecorder::disarm()
{
    if (!armed_) return;
    armed_ = false;
    qInfo() << "ClipRecorder: DISARMED — detection paused";
    emit armedChanged(false);
}

// ── incoming frames ───────────────────────────────────────────────────────────

void ClipRecorder::onNewFrame(const QByteArray& jpeg)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (state_ == State::Idle) {
        // Always keep the ring buffer rolling so pre-buffer is ready when armed
        ring_.emplace_back(now, jpeg);
        if (static_cast<int>(ring_.size()) > maxPreFrames_)
            ring_.pop_front();
    } else {
        clip_.emplace_back(now, jpeg);
        if (--postRemaining_ <= 0)
            finishClip();
    }
}

// ── trigger ───────────────────────────────────────────────────────────────────

void ClipRecorder::trigger(const QString& reason)
{
    if (!armed_) return;                 // silently ignore when disarmed
    if (state_ == State::Capturing) return;

    qInfo() << "ClipRecorder: triggered by" << reason;

    const QByteArray snapshot = ring_.empty() ? QByteArray{} : ring_.back().second;

    state_         = State::Capturing;
    reason_        = reason;
    postRemaining_ = postFrames_;

    clip_.assign(ring_.begin(), ring_.end());
    ring_.clear();

    emit triggered(reason, snapshot);
}

// ── encode and emit ───────────────────────────────────────────────────────────

void ClipRecorder::finishClip()
{
    state_ = State::Idle;
    if (clip_.empty()) return;

    const QString ts  = QString::number(QDateTime::currentSecsSinceEpoch());
    const QString tmp = QStringLiteral("/tmp/puppycam_%1").arg(ts);
    const QString out = QStringLiteral("%1/%2_%3.mp4").arg(kClipDir, ts, reason_);

    QDir().mkpath(tmp);
    QDir().mkpath(kClipDir);

    const int frameCount = static_cast<int>(clip_.size());
    for (int i = 0; i < frameCount; ++i) {
        QFile f(QStringLiteral("%1/frame_%2.jpg").arg(tmp).arg(i, 6, 10, QChar('0')));
        if (f.open(QIODevice::WriteOnly)) f.write(clip_[i].second);
    }
    clip_.clear();

    qInfo() << "ClipRecorder: encoding" << frameCount << "frames ->" << out;


    auto* proc = new QProcess(this);
    const QString framePat      = tmp + QStringLiteral("/frame_%06d.jpg");
    const QString capturedReason = reason_;

    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, proc, tmp, out, capturedReason](int exitCode, QProcess::ExitStatus) {
                if (exitCode == 0) {
                    qInfo() << "ClipRecorder: saved" << out;
                    emit clipReady(out, capturedReason);
                } else {
                    qWarning() << "ClipRecorder: ffmpeg error:"
                               << proc->readAllStandardError();
                }
                QDir d(tmp);
                for (const QString& f : d.entryList(QDir::Files)) d.remove(f);
                QDir().rmdir(tmp);
                proc->deleteLater();
            });

    proc->start(QStringLiteral("ffmpeg"), {
                                              // Video from pre-captured frames
                                              QStringLiteral("-framerate"), QString::number(fps_),
                                              QStringLiteral("-i"),         framePat,
                                              // NO audio — ALSA device is busy with AudioMonitor
                                              QStringLiteral("-c:v"),       QStringLiteral("libx264"),
                                              QStringLiteral("-pix_fmt"),   QStringLiteral("yuv420p"),
                                              QStringLiteral("-y"),         out
                                          });
}
