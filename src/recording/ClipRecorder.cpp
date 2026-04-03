#include "ClipRecorder.h"

#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QProcess>

static const QString kClipDir = QStringLiteral("/var/lib/puppycam/clips");

ClipRecorder::ClipRecorder(int preBufferSec, int postBufferSec, int fps, QObject* parent)
    : QObject(parent)
    , fps_(fps)
    , maxPreFrames_(preBufferSec * fps)
    , postFrames_(postBufferSec * fps)
{}

// ── incoming frames ───────────────────────────────────────────────────────────

void ClipRecorder::onNewFrame(const QByteArray& jpeg)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    if (state_ == State::Idle) {
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
    if (state_ == State::Capturing) return; // already recording

    qInfo() << "ClipRecorder: triggered by" << reason;
    state_         = State::Capturing;
    reason_        = reason;
    postRemaining_ = postFrames_;
    snapshot_      = ring_.empty() ? QByteArray{} : ring_.back().second;

    // Seed clip with everything in the pre-buffer
    clip_.assign(ring_.begin(), ring_.end());
    ring_.clear();
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

    // Write individual JPEG frames
    for (int i = 0; i < static_cast<int>(clip_.size()); ++i) {
        QFile f(QStringLiteral("%1/frame_%2.jpg").arg(tmp).arg(i, 6, 10, QChar('0')));
        if (f.open(QIODevice::WriteOnly)) f.write(clip_[i].second);
    }
    clip_.clear();

    qInfo() << "ClipRecorder: encoding" << clip_.size() << "frames ->" << out;

    // Encode with FFmpeg (async — won't block the event loop)
    auto* proc = new QProcess(this);
    const QString framePat = tmp + QStringLiteral("/frame_%06d.jpg");

    connect(proc,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this, proc, tmp, out](int exitCode, QProcess::ExitStatus) {
                if (exitCode == 0) {
                    qInfo() << "ClipRecorder: saved" << out;
                    emit clipReady(out, reason_, snapshot_);
                } else {
                    qWarning() << "ClipRecorder: ffmpeg error:"
                               << proc->readAllStandardError();
                }
                // Clean up temp frame dir
                QDir d(tmp);
                const QStringList files = d.entryList(QDir::Files);
                for (const QString& f : files) d.remove(f);
                QDir().rmdir(tmp);
                proc->deleteLater();
            });

    proc->start(QStringLiteral("ffmpeg"), {
                                              QStringLiteral("-framerate"), QString::number(fps_),
                                              QStringLiteral("-i"),         framePat,
                                              QStringLiteral("-c:v"),       QStringLiteral("libx264"),
                                              QStringLiteral("-pix_fmt"),   QStringLiteral("yuv420p"),
                                              QStringLiteral("-y"),
                                              out
                                          });
}
