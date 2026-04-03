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
    explicit ClipRecorder(int      preBufferSec  = 180,
                          int      postBufferSec = 120,
                          int      fps           = 10,
                          QObject* parent        = nullptr);

    bool isArmed() const { return armed_; }

public slots:
    void onNewFrame(const QByteArray& jpeg);
    void trigger(const QString& reason);
    void arm();
    void disarm();

signals:
    // Fired immediately at trigger — snapshot is the exact frame at that moment
    void triggered(const QString& reason, QByteArray snapshot);
    // Fired when mp4 is encoded and ready
    void clipReady(const QString& mp4Path, const QString& reason);
    // Fired when armed state changes
    void armedChanged(bool armed);

private:
    void finishClip();

    int     fps_;
    int     maxPreFrames_;
    int     postFrames_;
    bool    armed_ = false;  // disarmed by default — arm manually from the UI

    std::deque<std::pair<qint64, QByteArray>> ring_;

    enum class State { Idle, Capturing };
    State   state_         = State::Idle;
    int     postRemaining_ = 0;
    QString reason_;

    std::vector<std::pair<qint64, QByteArray>> clip_;
};
