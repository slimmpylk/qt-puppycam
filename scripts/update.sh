#!/usr/bin/env bash
set -euo pipefail

echo "[1/4] Pulling latest code..."
git pull --rebase

echo "[2/4] Building..."
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

echo "[3/4] Installing binary..."
sudo install -m 0755 build/puppycam /usr/local/bin/puppycam

echo "[4/4] Restarting service..."
sudo systemctl restart puppycam.service

echo "Done."
sudo systemctl status puppycam.service --no-pager
