#!/usr/bin/env bash
# update.sh — pull latest code, rebuild, restart service
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."

echo "[1/4] Pulling latest code..."
git -C "$REPO_DIR" pull --rebase

echo "[2/4] Building..."
cmake -S "$REPO_DIR" -B "$REPO_DIR/build" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$REPO_DIR/build"

echo "[3/4] Installing binary..."
sudo install -m 0755 "$REPO_DIR/build/qt-puppycam" /usr/local/bin/puppycam

echo "[4/4] Restarting service..."
sudo systemctl restart puppycam.service

echo ""
echo "✅  Done!"
sudo systemctl status puppycam.service --no-pager
