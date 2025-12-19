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
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Find blackbox-server directory
BLACKBOX_SERVER_DIR=""
if [ -d "$PROJECT_ROOT/blackbox-server" ]; then
    BLACKBOX_SERVER_DIR="$PROJECT_ROOT/blackbox-server"
else
    # Try to find it by looking for CMakeLists.txt
    BLACKBOX_SERVER_DIR="$(find "$PROJECT_ROOT" -maxdepth 3 -name "CMakeLists.txt" -path "*/blackbox-server/CMakeLists.txt" -exec dirname {} \; 2>/dev/null | head -1)"
    if [ -z "$BLACKBOX_SERVER_DIR" ]; then
        echo "Error: Could not find blackbox-server directory"
        echo "Searched in: $PROJECT_ROOT"
        echo "Current directory: $(pwd)"
        echo "Script directory: $SCRIPT_DIR"
        exit 1
    fi
fi

echo "Found blackbox-server at: $BLACKBOX_SERVER_DIR"
cd "$BLACKBOX_SERVER_DIR"
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo ""
echo "=== Setup Complete ==="
echo "Run server with: $BLACKBOX_SERVER_DIR/build/blackbox-server 6767"
echo "Or use: make run (from project root)"
echo "Test with: curl http://localhost:6767/vram"

