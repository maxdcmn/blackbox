#!/bin/bash
set -e

echo "=== Installing NVIDIA Toolkit and Dependencies ==="

# Detect OS
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
    VERSION=$VERSION_ID
else
    echo "Error: Cannot detect OS"
    exit 1
fi

echo "Detected OS: $OS $VERSION"

# Check if running as root for some operations
if [ "$EUID" -ne 0 ]; then 
    SUDO="sudo"
else
    SUDO=""
fi

# 1. Update package lists
echo ""
echo "=== Step 1: Updating package lists ==="
$SUDO apt update

# 2. Install build essentials
echo ""
echo "=== Step 2: Installing build tools ==="
$SUDO apt install -y \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    pkg-config

# 3. Check for NVIDIA drivers
echo ""
echo "=== Step 3: Checking NVIDIA drivers ==="
if command -v nvidia-smi &> /dev/null; then
    echo "✓ NVIDIA drivers detected"
    nvidia-smi --query-gpu=name,driver_version --format=csv,noheader
    DRIVER_VERSION=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader | head -1)
    echo "Driver version: $DRIVER_VERSION"
else
    echo "⚠ NVIDIA drivers not found. Installing..."
    
    # Add NVIDIA driver PPA (Ubuntu)
    if [ "$OS" = "ubuntu" ]; then
        $SUDO apt install -y software-properties-common
        $SUDO add-apt-repository -y ppa:graphics-drivers/ppa
        $SUDO apt update
    fi
    
    # Install NVIDIA drivers (latest stable)
    # For Ubuntu 22.04, prefer 535 or 525
    if $SUDO apt install -y nvidia-driver-535 nvidia-utils-535 2>/dev/null; then
        echo "✓ Installed nvidia-driver-535 and nvidia-utils-535"
    elif $SUDO apt install -y nvidia-driver-525 nvidia-utils-525 2>/dev/null; then
        echo "✓ Installed nvidia-driver-525 and nvidia-utils-525"
    elif $SUDO apt install -y nvidia-driver-470 nvidia-utils-470 2>/dev/null; then
        echo "✓ Installed nvidia-driver-470 and nvidia-utils-470"
    else
        echo "⚠ Could not install drivers automatically"
        echo "Try: sudo ubuntu-drivers autoinstall"
    fi
    
    echo ""
    echo "⚠⚠⚠ REBOOT REQUIRED ⚠⚠⚠"
    echo "After reboot, drivers and NVML library versions will match"
    echo "Run: sudo reboot"
    exit 0
fi

# Check for driver/library version mismatch
echo "Checking for driver/library version mismatch..."
if nvidia-smi &>/dev/null; then
    DRIVER_VERSION=$(nvidia-smi --query-gpu=driver_version --format=csv,noheader | head -1)
    echo "✓ Driver version: $DRIVER_VERSION"
    echo "✓ nvidia-smi works - driver and library versions match"
else
    echo "⚠ nvidia-smi failed - possible driver/library mismatch"
    echo "Try: sudo reboot"
    echo "Or reinstall: sudo apt install --reinstall nvidia-utils-535"
fi

# 4. Install CUDA Toolkit (optional but recommended)
echo ""
echo "=== Step 4: Installing CUDA Toolkit ==="
if command -v nvcc &> /dev/null; then
    echo "✓ CUDA toolkit detected"
    nvcc --version | head -4
else
    echo "Installing CUDA toolkit..."
    
    # For Ubuntu/Debian - install from apt
    $SUDO apt install -y nvidia-cuda-toolkit || {
        echo "⚠ CUDA toolkit installation failed. Continuing without it..."
        echo "Note: NVML should still work with just drivers."
    }
fi

# 5. Install NVML (runtime library + development headers)
echo ""
echo "=== Step 5: Installing NVML (runtime + headers) ==="

# Check for NVML runtime library
if find /usr -name libnvidia-ml.so* 2>/dev/null | grep -q libnvidia-ml.so; then
    echo "✓ NVML runtime library found"
else
    echo "Installing NVML runtime library..."
    # NVML runtime comes with nvidia-utils, try to install if missing
    $SUDO apt install -y nvidia-utils-535 2>/dev/null || \
    $SUDO apt install -y nvidia-utils-525 2>/dev/null || \
    $SUDO apt install -y nvidia-utils-520 2>/dev/null || \
    echo "⚠ Could not install NVML runtime. Ensure NVIDIA drivers are installed."
fi

# Check for NVML development headers
if find /usr -name nvml.h 2>/dev/null | grep -q nvml.h; then
    echo "✓ NVML headers already found"
else
    echo "Installing NVML development headers..."
    
    # Try different package names depending on what's available
    if $SUDO apt install -y libnvidia-ml-dev 2>/dev/null; then
        echo "✓ Installed libnvidia-ml-dev"
    elif $SUDO apt install -y cuda-nvml-dev-12-3 2>/dev/null; then
        echo "✓ Installed cuda-nvml-dev-12-3"
    elif $SUDO apt install -y cuda-nvml-dev-12-2 2>/dev/null; then
        echo "✓ Installed cuda-nvml-dev-12-2"
    elif $SUDO apt install -y cuda-nvml-dev-12-1 2>/dev/null; then
        echo "✓ Installed cuda-nvml-dev-12-1"
    elif $SUDO apt install -y cuda-nvml-dev-11-8 2>/dev/null; then
        echo "✓ Installed cuda-nvml-dev-11-8"
    else
        echo "⚠ Could not install NVML headers automatically."
        echo "Try manually:"
        echo "  sudo apt install -y libnvidia-ml-dev"
        echo "  OR"
        echo "  sudo apt install -y cuda-nvml-dev-<version>"
        echo ""
        echo "NVML headers are usually in:"
        echo "  /usr/include/nvml.h"
        echo "  /usr/local/cuda/include/nvml.h"
    fi
fi

# Verify NVML installation
echo ""
echo "Verifying NVML installation..."
if find /usr -name libnvidia-ml.so* 2>/dev/null | grep -q libnvidia-ml.so; then
    NVML_LIB=$(find /usr -name libnvidia-ml.so* 2>/dev/null | head -1)
    echo "✓ NVML runtime library found at: $NVML_LIB"
else
    echo "⚠ NVML runtime library not found. Server will run but NVML features will be disabled."
fi

if find /usr -name nvml.h 2>/dev/null | grep -q nvml.h; then
    NVML_PATH=$(find /usr -name nvml.h 2>/dev/null | head -1)
    echo "✓ NVML headers found at: $NVML_PATH"
else
    echo "⚠ NVML headers not found. The server will compile but NVML features will be disabled."
fi

# 6. Install Nsight Compute (optional, for advanced profiling)
echo ""
echo "=== Step 6: Installing Nsight Compute (optional) ==="
if command -v ncu &> /dev/null; then
    echo "✓ Nsight Compute (ncu) already installed"
    ncu --version 2>/dev/null || echo "  (version check unavailable)"
else
    echo "Nsight Compute not found. Installing..."
    echo "Note: This is optional but enables advanced GPU profiling metrics."
    
    # Download and install Nsight Compute
    NSIGHT_URL="https://developer.nvidia.com/downloads/assets/tools/secure/nsight-compute/2024_1_0/nsight-compute-linux-2024.1.0.15.tar.xz"
    NSIGHT_DIR="$HOME/nsight-compute"
    
    if [ ! -d "$NSIGHT_DIR" ]; then
        echo "Downloading Nsight Compute..."
        cd /tmp
        wget -q --show-progress "$NSIGHT_URL" -O nsight-compute.tar.xz || {
            echo "⚠ Download failed. You can install manually from:"
            echo "  https://developer.nvidia.com/nsight-compute"
            echo "  Or skip this step - it's optional."
        }
        
        if [ -f nsight-compute.tar.xz ]; then
            tar -xf nsight-compute.tar.xz
            mv nsight-compute-* "$NSIGHT_DIR"
            echo "✓ Nsight Compute extracted to $NSIGHT_DIR"
            echo "Add to PATH: export PATH=\$PATH:$NSIGHT_DIR"
            echo "Or create symlink: sudo ln -s $NSIGHT_DIR/ncu /usr/local/bin/ncu"
        fi
    else
        echo "✓ Nsight Compute already extracted at $NSIGHT_DIR"
    fi
fi

# 7. Verify installations
echo ""
echo "=== Step 7: Verification ==="
echo "Checking installed components..."

if command -v nvidia-smi &> /dev/null; then
    echo "✓ nvidia-smi: $(nvidia-smi --version | head -1)"
else
    echo "✗ nvidia-smi: NOT FOUND"
fi

if command -v nvcc &> /dev/null; then
    echo "✓ nvcc: $(nvcc --version | grep release | sed 's/.*release //' | sed 's/,.*//')"
else
    echo "⚠ nvcc: NOT FOUND (optional)"
fi

if find /usr -name libnvidia-ml.so* 2>/dev/null | grep -q libnvidia-ml.so; then
    echo "✓ NVML runtime library: FOUND"
else
    echo "✗ NVML runtime library: NOT FOUND"
fi

if find /usr -name nvml.h 2>/dev/null | grep -q nvml.h; then
    echo "✓ NVML headers: FOUND"
else
    echo "✗ NVML headers: NOT FOUND"
fi

if command -v ncu &> /dev/null; then
    echo "✓ ncu (Nsight Compute): FOUND"
else
    echo "⚠ ncu: NOT FOUND (optional)"
fi

if command -v cmake &> /dev/null; then
    echo "✓ cmake: $(cmake --version | head -1)"
else
    echo "✗ cmake: NOT FOUND"
fi

if command -v g++ &> /dev/null; then
    echo "✓ g++: $(g++ --version | head -1)"
else
    echo "✗ g++: NOT FOUND"
fi

echo ""
echo "=== Installation Complete ==="
echo ""
echo "Next steps:"
echo "1. If drivers were just installed, REBOOT: sudo reboot"
echo "2. Run the setup script: ./scripts/setup.sh"
echo "3. Or build manually:"
echo "   cd blackbox-server"
echo "   mkdir -p build && cd build"
echo "   cmake .. && make -j\$(nproc)"
echo ""

