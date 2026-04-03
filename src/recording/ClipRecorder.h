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
    explicit ClipRecorder(int preBufferSec  = 180,
                          int postBufferSec = 120,
                          int fps          = 10,
                          QObject* parent  = nullptr);

public slots:
    void onNewFrame(const QByteArray& jpeg);
    void trigger(const QString& reason);

signals:
    // Fired IMMEDIATELY at trigger moment — snapshot is the exact frame that triggered it
    void triggered(const QString& reason, QByteArray snapshot);

    // Fired when the mp4 file has been encoded and is ready on disk
    void clipReady(const QString& mp4Path, const QString& reason);

private:
    void finishClip();

    int fps_;
    int maxPreFrames_;
    int postFrames_;

    std::deque<std::pair<qint64, QByteArray>> ring_;

    enum class State { Idle, Capturing };
    State   state_         = State::Idle;
    int     postRemaining_ = 0;
    QString reason_;

    std::vector<std::pair<qint64, QByteArray>> clip_;
};
