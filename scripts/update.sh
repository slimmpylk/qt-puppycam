#!/usr/bin/env bash
# update.sh — SSH in and run this to pull + rebuild + restart
set -euo pipefail

echo "[1/4] Pulling latest code..."
git pull --rebase

echo "[2/4] Building..."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo "[3/4] Installing binary..."
sudo install -m 0755 build/qt-puppycam /usr/local/bin/qt-puppycam

echo "[4/4] Restarting service..."
sudo systemctl restart puppycam.service

echo ""
echo "✅  Done!"
sudo systemctl status puppycam.service --no-pager
