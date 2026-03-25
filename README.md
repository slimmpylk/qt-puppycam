# puppycam 🐾

Headless MJPEG webcam streamer for Raspberry Pi (and any Linux).  
Plug in a USB camera, run the service, watch the stream from anywhere via Tailscale.

Built with **Qt 6** (Core + Network) and **V4L2** — no OpenCV, no GStreamer, no heavy deps.

---

## How it works

```
USB camera → V4L2MjpegGrabber (thread) → FrameHub → HttpServer → browser / phone
```

- Captures raw MJPEG frames directly from the camera driver — zero re-encoding
- Serves a live stream at `/mjpeg`, a snapshot at `/snapshot.jpg`, and a viewer page at `/`
- Auto-detects the first MJPEG-capable USB camera on startup
- Retries automatically if the camera is unplugged and reconnected
- Runs as a systemd service, starts on boot

---

## Requirements

| Thing | Version |
|-------|---------|
| OS | Debian Bookworm (Pi OS 64-bit) or any modern Linux |
| Qt | 6.2+ |
| CMake | 3.22+ |
| Camera | Any USB webcam that supports MJPEG (most do) |

---

## Install on Raspberry Pi (first time)

```bash
# 1. Clone
git clone https://github.com/YOUR_USERNAME/puppycam.git
cd puppycam

# 2. Run the installer (builds, installs binary + systemd service)
./install.sh
```

The installer will:
- Install all build dependencies via `apt`
- Create a `puppycam` system user
- Build and install the binary to `/usr/local/bin/puppycam`
- Install the systemd service
- Write a default config to `/etc/puppycam.env`
- Enable and start the service

Check it's running:
```bash
sudo systemctl status puppycam
journalctl -u puppycam -f
```

---

## Configuration

Edit `/etc/puppycam.env` to change any setting, then restart:

```bash
sudo nano /etc/puppycam.env
sudo systemctl restart puppycam
```

| Variable | Default | Description |
|----------|---------|-------------|
| `PUPPYCAM_DEVICE` | `auto` | V4L2 device path, or `auto` to detect |
| `PUPPYCAM_WIDTH` | `1280` | Capture width in pixels |
| `PUPPYCAM_HEIGHT` | `720` | Capture height in pixels |
| `PUPPYCAM_FPS` | `10` | Target frames per second |
| `PUPPYCAM_PORT` | `8080` | HTTP port |

**Tip:** Use `auto` unless you have multiple cameras connected and need to pin a specific one.  
To find your device path: `v4l2-ctl --list-devices`

---

## Watching the stream

Open a browser and go to:

```
http://<pi-ip>:8080
```

If you're using **Tailscale**, use your Pi's Tailscale IP:
```bash
# On the Pi:
tailscale ip -4
```
Then open `http://100.x.x.x:8080` from your phone or laptop anywhere in the world.

| URL | What it does |
|-----|-------------|
| `/` | Live viewer page with fullscreen button |
| `/mjpeg` | Raw MJPEG stream (use in VLC, browser, etc.) |
| `/snapshot.jpg` | Single JPEG snapshot |

---

## Updating

SSH into the Pi, then:

```bash
cd ~/puppycam
./update.sh
```

This pulls the latest code, rebuilds, installs the new binary, and restarts the service.

---

## Building manually (dev / laptop)

```bash
# Install deps
sudo apt install -y build-essential cmake ninja-build qt6-base-dev qt6-base-dev-tools v4l-utils

# Build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run
./build/puppycam --device auto --port 8080
```

Command line options:
```
-d, --device   Device path or "auto"   (default: auto)
-p, --port     HTTP port               (default: 8080)
-W, --width    Capture width px        (default: 1280)
-H, --height   Capture height px       (default: 720)
-f, --fps      Target fps              (default: 10)
```

---

## Project structure

```
puppycam/
├── src/
│   ├── main.cpp
│   ├── capture/
│   │   ├── V4L2MjpegGrabber.h      # Camera capture thread
│   │   └── V4L2MjpegGrabber.cpp
│   ├── core/
│   │   ├── FrameHub.h              # Thread-safe latest-frame store
│   │   └── FrameHub.cpp
│   └── server/
│       ├── HttpServer.h            # Tiny HTTP server (MJPEG + snapshot)
│       └── HttpServer.cpp
├── packaging/
│   └── systemd/
│       └── puppycam.service
├── CMakeLists.txt
├── install.sh                      # First-time setup
└── update.sh                       # Pull + rebuild + restart
```

---

## License

MIT
