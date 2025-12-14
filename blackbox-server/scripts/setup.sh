#!/bin/bash
set -e

echo "=== Blackbox Server Setup ==="

# Check for NVIDIA GPU
if ! command -v nvidia-smi &> /dev/null; then
    echo "Warning: nvidia-smi not found. NVML may not work."
else
    echo "✓ NVIDIA GPU detected"
    nvidia-smi --query-gpu=name,memory.total --format=csv,noheader
fi

# Install dependencies
echo "Installing build dependencies..."
sudo apt update
sudo apt install -y build-essential cmake git

# Check for NVML
if ! find /usr -name nvml.h 2>/dev/null | grep -q nvml.h; then
    echo "Installing NVML development headers..."
    sudo apt install -y libnvidia-ml-dev || sudo apt install -y cuda-nvml-dev-12-3 || echo "Warning: NVML headers not found. Install manually."
else
    echo "✓ NVML headers found"
fi

# Build
echo "Building project..."
cd "$(dirname "$0")/.."
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo ""
echo "=== Setup Complete ==="
echo "Run server with: ./build/blackbox-server 6767"
echo "Or use: make run"
echo "Test with: curl http://localhost:6767/vram"

