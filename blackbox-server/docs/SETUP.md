# Setup Guide

Complete setup instructions for Blackbox Server.

## Quick Start

```bash
# 1. Install dependencies
./scripts/install_deps.sh

# 2. Build the server
./scripts/setup.sh

# 3. Run the server
./build/blackbox-server
```

## Prerequisites

### System Requirements

- **OS**: Linux (Ubuntu 22+)
- **GPU**: NVIDIA GPU with supported drivers
- **RAM**: 2GB+ available
- **Disk**: 1GB+ free space (for dependencies)

### Required Software

- **NVIDIA Drivers**: Version 470+ (recommended: 535+)
- **CUDA Toolkit**: Optional but recommended
- **Build Tools**: GCC 7+ or Clang 5+, CMake 3.15+

## Step-by-Step Installation

### Step 1: Install NVIDIA Drivers

```bash
# Check current drivers
nvidia-smi

# If not installed, install drivers
sudo apt update
sudo apt install -y nvidia-driver-535 nvidia-utils-535

# Reboot required after driver installation
sudo reboot
```

### Step 2: Install Build Dependencies

```bash
# Update package lists
sudo apt update

# Install build essentials
sudo apt install -y \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    pkg-config
```

### Step 3: Install NVML Development Headers

```bash
# Try standard package first
sudo apt install -y libnvidia-ml-dev

# Or CUDA-specific package (adjust version)
sudo apt install -y cuda-nvml-dev-12-3

# Verify installation
find /usr -name nvml.h
```

### Step 4: Install Nsight Compute (Optional)

Nsight Compute provides advanced GPU profiling metrics but is optional.

```bash
# Download from NVIDIA Developer site
# https://developer.nvidia.com/nsight-compute

# Extract to home directory
cd ~
tar -xf nsight-compute-linux-*.tar.xz
mv nsight-compute-* nsight-compute

# Add to PATH
export PATH=$PATH:~/nsight-compute

# Or create symlink
sudo ln -s ~/nsight-compute/ncu /usr/local/bin/ncu

# Verify
ncu --version
```

### Step 5: Build the Server

```bash
# Navigate to server directory
cd blackbox-server

# Run setup script
./scripts/setup.sh

# Or manually:
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Step 6: Verify Installation

```bash
# Check executable exists
ls -lh build/blackbox-server

# Test run
./build/blackbox-server

# In another terminal, test endpoint
curl http://localhost:6767/vram
```

## Automated Installation

Use the provided scripts for automated setup:

```bash
# Install all dependencies
./scripts/install_deps.sh

# Build the server
./scripts/setup.sh
```

## Running as a Service

### systemd Service

Create `/etc/systemd/system/blackbox-server.service`:

```ini
[Unit]
Description=Blackbox VRAM Monitor Server
After=network.target

[Service]
Type=simple
User=your-user
WorkingDirectory=/path/to/blackbox-server
ExecStart=/path/to/blackbox-server/build/blackbox-server 6767
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable blackbox-server
sudo systemctl start blackbox-server
sudo systemctl status blackbox-server
```

### Docker (Optional)

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    libnvidia-ml-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN ./scripts/setup.sh

EXPOSE 6767
CMD ["./build/blackbox-server"]
```

## Firewall Configuration

If accessing from remote machines:

```bash
# Allow port 6767
sudo ufw allow 6767/tcp

# Or for specific IP
sudo ufw allow from 192.168.1.0/24 to any port 6767
```

## Verification Checklist

- [ ] `nvidia-smi` works
- [ ] `nvml.h` found in `/usr/include` or `/usr/local/include`
- [ ] `ncu` command available (optional)
- [ ] Server builds successfully
- [ ] Server starts without errors
- [ ] `/vram` endpoint returns JSON
- [ ] `/vram/stream` endpoint streams data

## Troubleshooting

See [README.md](README.md#troubleshooting) for common issues and solutions.
