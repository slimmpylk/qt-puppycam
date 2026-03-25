#include "V4L2MjpegGrabber.h"
#include "../core/FrameHub.h"

#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QThread>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>

// ── helpers ──────────────────────────────────────────────────────────────────

static int xioctl(int fd, unsigned long req, void* arg) {
    for (;;) {
        int r = ::ioctl(fd, req, arg);
        if (r == -1 && errno == EINTR) continue;
        return r;
    }
}

struct MMapBuf {
    void*  start  = nullptr;
    size_t length = 0;
};

// ── static: device detection ─────────────────────────────────────────────────

QString V4L2MjpegGrabber::detectDevice()
{
    // 1. Prefer stable /dev/v4l/by-id/...-video-index0 symlinks.
    //    These survive reboots and USB-hub changes.
    QDir byId(QStringLiteral("/dev/v4l/by-id"));
    if (byId.exists()) {
        const QStringList entries =
            byId.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot);
        for (const QString& e : entries) {
            if (e.endsWith(QStringLiteral("-video-index0"))) {
                // Skip built-in cameras — they always have _0000 as their serial
                if (e.contains(QStringLiteral("_0000-")))
                    continue;
                return byId.filePath(e);
            }
        }
    }

    // 2. Scan /dev/video0..7 — pick first one that advertises MJPEG capture.
    for (int i = 0; i < 8; ++i) {
        const QString path = QStringLiteral("/dev/video%1").arg(i);
        int fd = ::open(path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;

        v4l2_fmtdesc fmtdesc{};
        fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        bool hasMjpeg = false;
        while (::ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
            if (fmtdesc.pixelformat == V4L2_PIX_FMT_MJPEG) {
                hasMjpeg = true;
                break;
            }
            ++fmtdesc.index;
        }
        ::close(fd);

        if (hasMjpeg)
            return path;
    }

    return {}; // nothing found
}

// ── constructor ───────────────────────────────────────────────────────────────

V4L2MjpegGrabber::V4L2MjpegGrabber(FrameHub* hub,
                                   const QString& device,
                                   int width, int height, int fps,
                                   QObject* parent)
    : QThread(parent)
    , hub_(hub)
    , dev_(device)
    , width_(width)
    , height_(height)
    , fps_(fps)
{}

// ── public stop ───────────────────────────────────────────────────────────────

void V4L2MjpegGrabber::stop() {
    stop_.storeRelease(1);
}

// ── thread entry: retry loop ──────────────────────────────────────────────────

void V4L2MjpegGrabber::run()
{
    while (!stop_.loadAcquire()) {
        // Resolve "auto" on every attempt so we pick up a newly plugged camera.
        QString resolved = dev_;
        if (resolved.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
            resolved = detectDevice();
            if (resolved.isEmpty()) {
                qInfo() << "No MJPEG camera found — retrying in"
                        << kRetryMs / 1000 << "s ...";
                sleepMs(kRetryMs);
                continue;
            }
            qInfo() << "Auto-detected camera:" << resolved;
        }

        const bool cleanStop = runOnce(resolved);
        if (cleanStop || stop_.loadAcquire())
            break;

        qInfo() << "Camera error on" << resolved
                << "— retrying in" << kRetryMs / 1000 << "s ...";
        sleepMs(kRetryMs);
    }
}

// ── one capture session ───────────────────────────────────────────────────────

bool V4L2MjpegGrabber::runOnce(const QString& resolvedDevice)
{
    const QByteArray devPath = resolvedDevice.toLocal8Bit();
    int fd = ::open(devPath.constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        qWarning() << "Cannot open" << resolvedDevice << "errno" << errno;
        return false; // trigger retry
    }

    // ── query capabilities ────────────────────────────────────────────────────
    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        qInfo() << "Device:" << resolvedDevice
                << "driver:" << reinterpret_cast<const char*>(cap.driver)
                << "card:"   << reinterpret_cast<const char*>(cap.card);
    }

    // ── set MJPEG format ──────────────────────────────────────────────────────
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = static_cast<unsigned>(width_);
    fmt.fmt.pix.height      = static_cast<unsigned>(height_);
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        qWarning() << "VIDIOC_S_FMT failed errno" << errno;
        ::close(fd);
        return false;
    }
    qInfo() << "Format:" << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height
            << "fourcc=0x" << QString::number(fmt.fmt.pix.pixelformat, 16);

    // ── set FPS (best-effort) ─────────────────────────────────────────────────
    v4l2_streamparm parm{};
    parm.type                                  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator   = 1;
    parm.parm.capture.timeperframe.denominator = static_cast<unsigned>(fps_);
    xioctl(fd, VIDIOC_S_PARM, &parm);

    // ── request mmap buffers ──────────────────────────────────────────────────
    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        qWarning() << "VIDIOC_REQBUFS failed errno" << errno;
        ::close(fd);
        return false;
    }

    std::vector<MMapBuf> bufs(req.count);
    for (unsigned i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QUERYBUF failed errno" << errno;
            ::close(fd);
            return false;
        }
        bufs[i].length = buf.length;
        bufs[i].start  = mmap(nullptr, buf.length,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             fd, buf.m.offset);
        if (bufs[i].start == MAP_FAILED) {
            qWarning() << "mmap failed errno" << errno;
            ::close(fd);
            return false;
        }
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QBUF failed errno" << errno;
            ::close(fd);
            return false;
        }
    }

    // ── start streaming ───────────────────────────────────────────────────────
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        qWarning() << "VIDIOC_STREAMON failed errno" << errno;
        ::close(fd);
        return false;
    }
    qInfo() << "Streaming from" << resolvedDevice;

    // ── capture loop ──────────────────────────────────────────────────────────
    bool errorOccurred = false;
    while (!stop_.loadAcquire()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        timeval tv{ .tv_sec = 2, .tv_usec = 0 };

        int r = select(fd + 1, &fds, nullptr, nullptr, &tv);
        if (r == 0) continue;  // timeout — check stop_ and loop
        if (r  < 0) {
            if (errno == EINTR) continue;
            qWarning() << "select() failed errno" << errno;
            errorOccurred = true;
            break;
        }

        v4l2_buffer buf{};
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            qWarning() << "VIDIOC_DQBUF failed errno" << errno
                       << "(camera unplugged?)";
            errorOccurred = true;
            break;
        }

        if (buf.bytesused > 0 && buf.index < bufs.size()) {
            const auto* p = static_cast<const char*>(bufs[buf.index].start);
            hub_->setLatestJpeg(QByteArray(p, static_cast<int>(buf.bytesused)));
        }

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QBUF failed errno" << errno;
            errorOccurred = true;
            break;
        }
    }

    // ── cleanup ───────────────────────────────────────────────────────────────
    xioctl(fd, VIDIOC_STREAMOFF, &type);
    for (auto& b : bufs)
        if (b.start && b.start != MAP_FAILED)
            munmap(b.start, b.length);
    ::close(fd);

    return !errorOccurred; // true = clean stop_, false = retry
}

// ── sleep helper (interruptible) ─────────────────────────────────────────────
void V4L2MjpegGrabber::sleepMs(int ms)
{
    for (int i = 0; i < ms / 100 && !stop_.loadAcquire(); ++i)
        QThread::msleep(100);
}
