#pragma once
#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <deque>
#include <utility>
#include <vector>

class ClipRecorder : public QObject {
    Q_OBJECT
public:
    // preBufferSec  : how many seconds of footage to keep before a trigger
    // postBufferSec : how many seconds to record after a trigger
    // fps           : capture fps (used to size the frame buffer)
    explicit ClipRecorder(int preBufferSec  = 180,
                          int postBufferSec = 120,
                          int fps          = 10,
                          QObject* parent  = nullptr);

public slots:
    void onNewFrame(const QByteArray& jpeg);
    void trigger(const QString& reason);  // "motion" | "sound"

signals:
    // Emitted when the mp4 file has been written to disk
    void clipReady(const QString& mp4Path, const QString& reason, QByteArray snapshot);

private:
    void finishClip();

    int fps_;
    int maxPreFrames_;
    int postFrames_;

    // Rolling pre-event buffer
    std::deque<std::pair<qint64, QByteArray>> ring_;

    enum class State { Idle, Capturing };
    State  state_         = State::Idle;
    int    postRemaining_ = 0;
    QString reason_;
    QByteArray snapshot_;   // frame at trigger moment → sent to Telegram

    // Assembled clip (pre + post frames)
    std::vector<std::pair<qint64, QByteArray>> clip_;
};
