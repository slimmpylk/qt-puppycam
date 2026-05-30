# qt-puppycam 🐾

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Qt](https://img.shields.io/badge/Qt-6.x-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Raspberry%20Pi-red.svg)]()
[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)]()

I wanted a security camera that would let me check in on my puppy from anywhere, and I wanted to build it properly, not glue together a bunch of Python scripts. So I wrote one from scratch in C++20 and Qt 6.

The result is a headless system that runs on a Raspberry Pi, streams live video over MJPEG, watches for motion and sound, records dashcam-style clips when something happens, and sends them straight to Telegram. It runs as a systemd service and handles camera disconnects, audio device changes, and remote updates without any manual intervention.

No OpenCV. No GStreamer. No Electron. Raw V4L2 for camera capture, ALSA for audio, and a hand-rolled HTTP server.

---

## What it does

The camera streams MJPEG at up to 1280×720 to a simple web page that works in any browser, VLC, or ffplay. Auto camera detection picks up the right USB webcam on startup and skips built-in ones. If the camera gets unplugged, it keeps retrying until it comes back.

Motion detection runs frame-differencing at 320×180 grayscale, fast, no GPU needed, tunable threshold. Sound detection reads RMS amplitude from the camera microphone via ALSA. When either triggers, the system records a clip using a pre-event ring buffer, sends an instant snapshot to Telegram, and uploads the full MP4 when it finishes. The local file is deleted after a confirmed upload.

A live clock burned into the top-left corner of every frame makes it easy to confirm the feed is actually live at a glance.

The web UI has an arm/disarm toggle so alerts can be silenced when you are home, without stopping the stream or the recording buffer.

---

## Architecture

```
USB camera (MJPEG)
    │
    ▼
V4L2MjpegGrabber          ← capture thread, V4L2 mmap buffers
    │
    ▼
FrameHub                   ← thread-safe frame store, emits Qt signal per frame
    ├──────────────────────────────────────┐
    ▼                                      ▼
MotionDetector             ← frame diff   ClipRecorder  ← ring buffer + post-buffer
    │                                      │     ▲
AudioMonitor               ← ALSA RMS ────┘     │ frames
    │                                      │
    └──── trigger() ───────────────────────┘
                                           │
                                    triggered() → TelegramNotifier → sendPhoto() immediately
                                    clipReady()  → TelegramNotifier → sendVideo() + delete
    │
    ▼
HttpServer                 ← serves /  /mjpeg  /snapshot.jpg
```

V4L2 and ALSA each run in a dedicated `QThread`. All detection and recording runs on the Qt main event loop via `Qt::QueuedConnection`, with no mutexes needed beyond FrameHub's `QReadWriteLock`.

---

## Tech stack

| Component | Technology |
|-----------|-----------|
| Language | C++20 |
| Framework | Qt 6 (Core, Network, Gui) |
| Camera capture | V4L2 (Linux kernel API, mmap streaming) |
| Audio capture | ALSA (raw PCM, 16kHz mono) |
| Video encoding | FFmpeg (subprocess, H.264/MP4) |
| HTTP server | Custom (raw TCP, QTcpServer) |
| Notifications | Telegram Bot API (multipart HTTP) |
| Service management | systemd |
| Build system | CMake 3.22+ + Ninja |
| Target platform | Raspberry Pi 5, Debian Trixie / Bookworm |

---

## Requirements

| Requirement | Version |
|-------------|---------|
| OS | Debian Bookworm / Trixie (Raspberry Pi OS 64-bit) |
| Qt | 6.2+ |
| CMake | 3.22+ |
| FFmpeg | Any recent version |
| Camera | Any USB webcam supporting MJPEG |
| (Optional) | Tailscale for remote access |

---

## Installation

```bash
git clone https://github.com/slimmpylk/qt-puppycam.git
cd qt-puppycam
./scripts/install.sh
```

The installer handles dependencies via apt, creates a dedicated system user with video and audio group access, builds the binary in Release mode, sets up the systemd service, and creates the clips directory. After that the service starts automatically on every boot.

Verify it is running:
```bash
sudo systemctl status puppycam
journalctl -u puppycam -f
```

---

## Configuration

All settings live in `/etc/puppycam.env` and take effect after a service restart. No recompiling needed.

```bash
sudo nano /etc/puppycam.env
sudo systemctl restart puppycam
```

| Variable | Default | Description |
|----------|---------|-------------|
| `PUPPYCAM_DEVICE` | `auto` | V4L2 device path, or `auto` to detect |
| `PUPPYCAM_WIDTH` | `1280` | Capture width (px) |
| `PUPPYCAM_HEIGHT` | `720` | Capture height (px) |
| `PUPPYCAM_FPS` | `10` | Frames per second |
| `PUPPYCAM_PORT` | `8080` | HTTP port |
| `PUPPYCAM_MOTION_THRESHOLD` | `3000` | Changed pixels at 320×180 to trigger (lower = more sensitive) |
| `PUPPYCAM_SOUND_THRESHOLD` | `1500` | RMS amplitude to trigger, 0–32767 (lower = more sensitive) |
| `PUPPYCAM_AUDIO_DEVICE` | `auto` | ALSA device string, or `auto` |
| `PUPPYCAM_PRE_BUFFER_SEC` | `180` | Seconds of footage kept before a trigger |
| `PUPPYCAM_POST_BUFFER_SEC` | `120` | Seconds recorded after a trigger |
| `PUPPYCAM_TELEGRAM_TOKEN` | _(empty)_ | Telegram bot token from @BotFather |
| `PUPPYCAM_TELEGRAM_CHAT_ID` | _(empty)_ | Your Telegram chat ID from @userinfobot |

---

## Watching the stream

Open `http://<device-ip>:8080` in any browser. With Tailscale, get the Pi's IP with `tailscale ip -4` and use that instead.

| Endpoint | Description |
|----------|-------------|
| `/` | Browser viewer with fullscreen support |
| `/mjpeg` | Raw MJPEG stream (works in VLC, browsers, ffplay) |
| `/snapshot.jpg` | Latest JPEG frame |

---

## Telegram notifications

1. Message `@BotFather` on Telegram, create a new bot, copy the token
2. Message `@userinfobot`, copy your chat ID
3. Add both to `/etc/puppycam.env`
4. `sudo systemctl restart puppycam`

When something triggers you get an instant snapshot at the moment of detection and a full MP4 clip once it finishes recording. The clip includes the pre-event buffer so you see what led up to the trigger, not just the aftermath.

---

## Updating

```bash
cd ~/qt-puppycam
./scripts/update.sh
```

Pulls from GitHub, rebuilds in Release mode, installs the new binary, restarts the service.

---

## Project structure

```
qt-puppycam/
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── FrameHub.h/.cpp          # Thread-safe frame store with Qt signal
│   ├── capture/
│   │   ├── V4L2MjpegGrabber.h/.cpp  # V4L2 mmap capture thread, auto-detect, retry loop
│   ├── server/
│   │   ├── HttpServer.h/.cpp        # Minimal HTTP server, multipart MJPEG streaming
│   ├── detection/
│   │   ├── MotionDetector.h/.cpp    # Frame-differencing motion detection
│   │   ├── AudioMonitor.h/.cpp      # ALSA RMS sound level detection
│   ├── recording/
│   │   ├── ClipRecorder.h/.cpp      # Dashcam ring buffer, FFmpeg encoding
│   └── notify/
│       ├── TelegramNotifier.h/.cpp  # Telegram Bot API, photo + video upload
├── packaging/
│   └── systemd/puppycam.service
├── scripts/
│   ├── install.sh                   # First-time setup
│   └── update.sh                    # Pull + rebuild + restart
└── CMakeLists.txt
```

---

## Building for development

```bash
sudo apt install -y build-essential cmake ninja-build \
  qt6-base-dev qt6-base-dev-tools ffmpeg v4l-utils

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

./build/qt-puppycam --device auto --port 8080
```

---
