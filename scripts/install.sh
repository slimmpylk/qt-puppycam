#!/usr/bin/env bash
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
sudo usermod -aG video puppycam || true

echo "[3/6] Building puppycam..."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo "[4/6] Installing binary..."
sudo install -m 0755 build/puppycam /usr/local/bin/puppycam

echo "[5/6] Installing systemd service..."
sudo install -m 0644 packaging/systemd/puppycam.service /etc/systemd/system/puppycam.service
sudo systemctl daemon-reload

echo "[6/6] Enabling and starting service..."
sudo systemctl enable --now puppycam.service

echo "Done."
echo "Check status: sudo systemctl status puppycam --no-pager"
