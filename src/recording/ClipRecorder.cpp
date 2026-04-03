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

void ClipRecorder::trigger(const QString& reason)
{
    if (state_ == State::Capturing) return;

    qInfo() << "ClipRecorder: triggered by" << reason;

    // Grab snapshot BEFORE moving ring_ into clip_ — this is the exact trigger frame
    const QByteArray snapshot = ring_.empty() ? QByteArray{} : ring_.back().second;

    state_         = State::Capturing;
    reason_        = reason;
    postRemaining_ = postFrames_;

    clip_.assign(ring_.begin(), ring_.end());
    ring_.clear();

    // Emit immediately so Telegram sends the photo right now, not after 2 minutes
    emit triggered(reason, snapshot);
}

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
                                              QStringLiteral("-framerate"), QString::number(fps_),
                                              QStringLiteral("-i"),         framePat,
                                              QStringLiteral("-c:v"),       QStringLiteral("libx264"),
                                              QStringLiteral("-pix_fmt"),   QStringLiteral("yuv420p"),
                                              QStringLiteral("-y"),         out
                                          });
}
