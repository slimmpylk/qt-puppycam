#!/usr/bin/env bash
# install.sh — first-time setup for puppycam on Raspberry Pi 5 (Debian Bookworm/Trixie)
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."

echo "[1/7] Installing dependencies..."
sudo apt update
sudo apt install -y \
  git build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools \
  libasound2-dev \
  ffmpeg \
  v4l-utils

echo "[2/7] Creating service user..."
if ! id puppycam >/dev/null 2>&1; then
  sudo useradd -r -m -s /usr/sbin/nologin puppycam
fi
sudo usermod -aG video,audio puppycam || true

echo "[3/7] Building puppycam..."
cmake -S "$REPO_DIR" -B "$REPO_DIR/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$REPO_DIR/build"

echo "[4/7] Installing binary..."
sudo install -m 0755 "$REPO_DIR/build/qt-puppycam" /usr/local/bin/puppycam

echo "[5/7] Installing systemd service..."
sudo install -m 0644 "$REPO_DIR/packaging/systemd/puppycam.service" \
    /etc/systemd/system/puppycam.service
sudo systemctl daemon-reload

echo "[5.5/7] Creating clip storage directory..."
sudo mkdir -p /var/lib/puppycam/clips
sudo chown puppycam:puppycam /var/lib/puppycam/clips

echo "[6/7] Creating /etc/puppycam.env (if missing)..."
if [ ! -f /etc/puppycam.env ]; then
  sudo tee /etc/puppycam.env >/dev/null <<'EOC'
# ── Camera ────────────────────────────────────────────────────────────────────
# auto = detect first MJPEG-capable USB camera
PUPPYCAM_DEVICE=auto
PUPPYCAM_WIDTH=1280
PUPPYCAM_HEIGHT=720
PUPPYCAM_FPS=10
PUPPYCAM_PORT=8080

# ── Motion detection ──────────────────────────────────────────────────────────
# Number of changed pixels at 320x180 grayscale to trigger (lower = more sensitive)
PUPPYCAM_MOTION_THRESHOLD=3000

# ── Sound detection ───────────────────────────────────────────────────────────
# auto = detect first USB audio device
PUPPYCAM_AUDIO_DEVICE=auto
# RMS amplitude to trigger (0-32767, lower = more sensitive)
PUPPYCAM_SOUND_THRESHOLD=1500

# ── Clip recording ────────────────────────────────────────────────────────────
# Seconds of footage to keep before a trigger (dashcam pre-buffer)
PUPPYCAM_PRE_BUFFER_SEC=180
# Seconds to record after a trigger
PUPPYCAM_POST_BUFFER_SEC=120

# ── Telegram notifications ────────────────────────────────────────────────────
# Get token from @BotFather on Telegram
# Get your chat_id by messaging @userinfobot on Telegram
PUPPYCAM_TELEGRAM_TOKEN=
PUPPYCAM_TELEGRAM_CHAT_ID=
EOC
  echo "  Wrote default /etc/puppycam.env"
else
  echo "  /etc/puppycam.env already exists — leaving it unchanged"
  echo "  NOTE: new variables may have been added — check the README for new options"
fi

echo "[7/7] Enabling and starting service..."
sudo systemctl enable --now puppycam.service

echo ""
echo "✅  Done!"
echo ""
echo "   Stream  : http://$(hostname -I | awk '{print $1}'):8080"
echo "   Clips   : /var/lib/puppycam/clips/"
echo "   Config  : sudo nano /etc/puppycam.env"
echo "   Restart : sudo systemctl restart puppycam"
echo "   Logs    : journalctl -u puppycam -f"
echo ""
echo "   To enable Telegram notifications:"
echo "   1. Message @BotFather on Telegram → /newbot → copy the token"
echo "   2. Message @userinfobot → copy your chat_id"
echo "   3. sudo nano /etc/puppycam.env  (fill in TOKEN + CHAT_ID)"
echo "   4. sudo systemctl restart puppycam"
