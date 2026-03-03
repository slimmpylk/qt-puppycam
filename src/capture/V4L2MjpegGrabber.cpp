#include "V4L2MjpegGrabber.h"
#include "../core/FrameHub.h"

#include <QtCore/QByteArray>
#include <QtCore/QDebug>

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <vector>

static int xioctl(int fd, unsigned long req, void* arg) {
    for (;;) {
        int r = ioctl(fd, req, arg);
        if (r == -1 && errno == EINTR) continue;
        return r;
    }
}

struct MMapBuf {
    void*  start = nullptr;
    size_t length = 0;
};

V4L2MjpegGrabber::V4L2MjpegGrabber(FrameHub* hub, QString device, int width, int height, int fps, QObject* parent)
    : QThread(parent), hub_(hub), dev_(std::move(device)), width_(width), height_(height), fps_(fps) {}

void V4L2MjpegGrabber::stop() {
    stop_.storeRelease(1);
}

void V4L2MjpegGrabber::run() {
    const QByteArray devPath = dev_.toLocal8Bit();
    int fd = ::open(devPath.constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        qWarning() << "Failed to open" << dev_ << "errno" << errno;
        return;
    }

    v4l2_capability cap{};
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        qInfo() << "Using device:" << dev_
                << "driver:" << reinterpret_cast<const char*>(cap.driver)
                << "card:" << reinterpret_cast<const char*>(cap.card)
                << "bus:" << reinterpret_cast<const char*>(cap.bus_info);
    }

    // Set format to MJPG
    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        qWarning() << "VIDIOC_S_FMT failed errno" << errno;
        ::close(fd);
        return;
    }

    // Set FPS (best-effort)
    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps_;
    xioctl(fd, VIDIOC_S_PARM, &parm);

    // Request buffers
    v4l2_requestbuffers req{};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        qWarning() << "VIDIOC_REQBUFS failed/insufficient buffers errno" << errno;
        ::close(fd);
        return;
    }
    qInfo() << "Negotiated format:"
            << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height
            << "fourcc=0x" << QString::number(fmt.fmt.pix.pixelformat, 16)
            << "sizeimage=" << fmt.fmt.pix.sizeimage;

    std::vector<MMapBuf> bufs(req.count);

    for (unsigned i = 0; i < req.count; ++i) {
        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QUERYBUF failed errno" << errno;
            ::close(fd);
            return;
        }

        bufs[i].length = buf.length;
        bufs[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (bufs[i].start == MAP_FAILED) {
            qWarning() << "mmap failed errno" << errno;
            ::close(fd);
            return;
        }

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QBUF failed errno" << errno;
            ::close(fd);
            return;
        }
    }

    // Start streaming
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        qWarning() << "VIDIOC_STREAMON failed errno" << errno;
        ::close(fd);
        return;
    }

    qInfo() << "Capturing MJPG from" << dev_ << "@" << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height;

    while (!stop_.loadAcquire()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);

        timeval tv{};
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int r = select(fd + 1, &fds, nullptr, nullptr, &tv);
        if (r <= 0) continue;

        v4l2_buffer buf{};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) continue;
            qWarning() << "VIDIOC_DQBUF failed errno" << errno;
            break;
        }

        // buf.bytesused contains the JPEG size
        if (buf.bytesused > 0 && buf.index < bufs.size()) {
            const auto* p = static_cast<const char*>(bufs[buf.index].start);
            QByteArray jpeg(p, static_cast<int>(buf.bytesused));
            hub_->setLatestJpeg(std::move(jpeg));
        }

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QBUF failed errno" << errno;
            break;
        }
    }

    xioctl(fd, VIDIOC_STREAMOFF, &type);

    for (auto& b : bufs) {
        if (b.start && b.start != MAP_FAILED) munmap(b.start, b.length);
    }
    ::close(fd);
}
