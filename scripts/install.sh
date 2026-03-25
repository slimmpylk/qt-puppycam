#!/usr/bin/env bash
# install.sh — first-time setup for puppycam on Raspberry Pi 5 (Debian Bookworm)
set -euo pipefail

echo "[1/6] Installing dependencies..."
sudo apt update
sudo apt install -y \
  git build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools \
  v4l-utils

echo "[2/6] Creating service user..."
if ! id puppycam >/dev/null 2>&1; then
  sudo useradd -r -m -s /usr/sbin/nologin puppycam
fi
# Must be in the 'video' group to access /dev/video*
sudo usermod -aG video puppycam || true

echo "[3/6] Building puppycam..."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo "[4/6] Installing binary..."
sudo install -m 0755 build/puppycam /usr/local/bin/puppycam

echo "[5/6] Installing systemd service..."
sudo install -m 0644 packaging/systemd/puppycam.service \
    /etc/systemd/system/puppycam.service
sudo systemctl daemon-reload

echo "[5.5/6] Creating /etc/puppycam.env (if missing)..."
if [ ! -f /etc/puppycam.env ]; then
  sudo tee /etc/puppycam.env >/dev/null <<'EOC'
# PuppyCam runtime config
# -----------------------------------------------------------
# PUPPYCAM_DEVICE=auto        picks the first MJPEG-capable camera automatically
# PUPPYCAM_DEVICE=/dev/video0 pins a specific device
# -----------------------------------------------------------
PUPPYCAM_DEVICE=auto
PUPPYCAM_WIDTH=1280
PUPPYCAM_HEIGHT=720
PUPPYCAM_FPS=10
PUPPYCAM_PORT=8080
EOC
  echo "  Wrote default /etc/puppycam.env (DEVICE=auto)"
else
  echo "  /etc/puppycam.env already exists — leaving it unchanged"
fi

echo "[6/6] Enabling and starting service..."
sudo systemctl enable --now puppycam.service

echo ""
echo "✅  Done!"
echo "   Logs : journalctl -u puppycam -f"
echo "   Config: sudo nano /etc/puppycam.env  (then: sudo systemctl restart puppycam)"
echo "   Status: sudo systemctl status puppycam --no-pager"
