# qt-puppycam 🐾

> Headless MJPEG webcam security system for Raspberry Pi — built from scratch in C++20 and Qt 6.

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Qt](https://img.shields.io/badge/Qt-6.x-green.svg)](https://www.qt.io/)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Raspberry%20Pi-red.svg)]()
[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)]()

A production-ready, plug-and-play webcam security system. Connect any USB webcam, run one script, and get a live stream accessible from anywhere in the world — with automatic motion and sound detection, dashcam-style clip recording, and Telegram notifications.

No OpenCV. No GStreamer. No Electron. Pure Qt 6, V4L2, and ALSA.

---

## Features

- **Live MJPEG stream** — zero-latency stream viewable in any browser or VLC
- **Auto camera detection** — detects any MJPEG-capable USB camera automatically, skips built-in webcams
- **Plug-and-play resilience** — automatically reconnects if the camera is unplugged
- **Motion detection** — frame-differencing algorithm at 320×180 grayscale, tunable threshold
- **Sound detection** — RMS amplitude monitoring via ALSA on the camera's built-in microphone
- **Dashcam-style clip recording** — configurable pre-event ring buffer + post-event recording (like a dashcam)
- **Telegram notifications** — instant snapshot photo on trigger, full MP4 clip uploaded when ready
- **Auto cleanup** — local clip files deleted after confirmed Telegram upload
- **Zero-config deployment** — single `install.sh` sets up everything including systemd service
- **Remote updates** — `update.sh` pulls, rebuilds, and restarts in one command
- **Tailscale ready** — binds to all interfaces, accessible over Tailscale from anywhere

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

**Threading model:** V4L2 and ALSA each run in a dedicated `QThread`. All detection and recording runs on the Qt main event loop via `Qt::QueuedConnection` — no mutexes needed beyond FrameHub's `QReadWriteLock`.

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
# Clone the repository
git clone https://github.com/slimmpylk/qt-puppycam.git
cd qt-puppycam

# Run the installer — handles everything
./scripts/install.sh
```

The installer will:
- Install all build and runtime dependencies via `apt`
- Create a dedicated `puppycam` system user with `video` and `audio` group access
- Build the binary with CMake + Ninja in Release mode
- Install the systemd service (auto-starts on boot)
- Create `/etc/puppycam.env` with sensible defaults
- Create `/var/lib/puppycam/clips/` for clip storage

Verify it's running:
```bash
sudo systemctl status puppycam
journalctl -u puppycam -f
```

---

## Configuration

All runtime settings live in `/etc/puppycam.env` — **no recompiling required** to change any of them.

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

```
http://<device-ip>:8080
```

With Tailscale:
```bash
tailscale ip -4   # get the Pi's Tailscale IP
```

| Endpoint | Description |
|----------|-------------|
| `/` | Browser viewer with fullscreen support |
| `/mjpeg` | Raw MJPEG stream (works in VLC, browsers, ffplay) |
| `/snapshot.jpg` | Latest JPEG frame |

---

## Telegram notifications

1. Open Telegram → message `@BotFather` → `/newbot` → copy the token
2. Message `@userinfobot` → copy your chat ID
3. Add both to `/etc/puppycam.env`
4. `sudo systemctl restart puppycam`

On each trigger you receive:
- **Instant snapshot** — the exact frame at the moment of detection
- **Full MP4 clip** — pre-buffer + post-buffer, uploaded when ready, local file deleted on success

---

## Updating

```bash
cd ~/qt-puppycam
./scripts/update.sh
```

Pulls latest code from GitHub, rebuilds in Release mode, installs the new binary, restarts the service.

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
