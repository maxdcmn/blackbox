# Blackbox Server Setup Guide for GPU VM

## Prerequisites

### 1. NVIDIA Drivers and CUDA
```bash
# Check if NVIDIA drivers are installed
nvidia-smi

# If not installed, install NVIDIA drivers (Ubuntu/Debian):
sudo apt update
sudo apt install -y nvidia-driver-535 nvidia-utils-535

# Install CUDA toolkit (if needed)
sudo apt install -y nvidia-cuda-toolkit

# Reboot if drivers were just installed
sudo reboot
```

### 2. NVML Library
```bash
# Install NVML development headers (usually comes with NVIDIA drivers)
# For Ubuntu/Debian:
sudo apt install -y libnvidia-ml-dev

# Or if using CUDA toolkit:
sudo apt install -y cuda-nvml-dev-12-3  # Adjust version as needed
```

### 3. Build Tools
```bash
# Install essential build tools
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    git \
    curl \
    wget
```

## Building the Project

### 1. Clone/Transfer the Project
```bash
# If using git:
git clone <your-repo-url>
cd blackbox/blackbox-server

# Or transfer files via SCP:
# scp -r blackbox-server user@gpu-vm:/path/to/destination
```

### 2. Build
```bash
# Create build directory
mkdir -p build
cd build

# Configure and build (this will download Boost and Abseil automatically)
cmake ..
make -j$(nproc)

# The executable will be at: build/blackbox-server
```

**Note:** First build will take longer as it downloads Boost (~100MB) and builds the system library.

## Running the Server

### Basic Usage
```bash
# Run on default port 8080
./build/blackbox-server

# Or specify a custom port
./build/blackbox-server 9000
```

### Test the Endpoint
```bash
# From the same machine
curl http://localhost:8080/vram

# From another machine
curl http://<vm-ip>:8080/vram
```

Expected response:
```json
{"total_bytes":34359738368,"used_bytes":8589934592,"free_bytes":25769803776,"used_percent":25.00}
```

## Running as a Service (systemd)

### 1. Create Service File
```bash
sudo nano /etc/systemd/system/blackbox-server.service
```

Add:
```ini
[Unit]
Description=Blackbox VRAM Monitor Server
After=network.target

[Service]
Type=simple
User=your-username
WorkingDirectory=/path/to/blackbox/blackbox-server
ExecStart=/path/to/blackbox/blackbox-server/build/blackbox-server 8080
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

### 2. Enable and Start
```bash
# Reload systemd
sudo systemctl daemon-reload

# Enable service (starts on boot)
sudo systemctl enable blackbox-server

# Start service
sudo systemctl start blackbox-server

# Check status
sudo systemctl status blackbox-server

# View logs
sudo journalctl -u blackbox-server -f
```

## Firewall Configuration

```bash
# Allow port 8080 (Ubuntu/Debian with ufw)
sudo ufw allow 8080/tcp
sudo ufw reload

# Or with iptables
sudo iptables -A INPUT -p tcp --dport 8080 -j ACCEPT
```

## Troubleshooting

### NVML Not Found
```bash
# Check if NVML headers exist
find /usr -name nvml.h 2>/dev/null

# If missing, install:
sudo apt install -y libnvidia-ml-dev

# Or manually set include path in CMakeLists.txt
```

### Build Fails on Boost Download
```bash
# Check internet connection
ping -c 3 archives.boost.io

# Or manually download and extract Boost to a local directory
# Then modify CMakeLists.txt to use local path
```

### Permission Denied on Port
```bash
# Use a port > 1024, or run with sudo (not recommended)
# Better: use systemd service with proper user permissions
```

### GPU Not Detected
```bash
# Verify GPU is visible
nvidia-smi

# Check NVML initialization in code
# Server will return zeros if NVML fails
```

## Quick Setup Script

Save as `setup.sh`:
```bash
#!/bin/bash
set -e

echo "Installing dependencies..."
sudo apt update
sudo apt install -y build-essential cmake git libnvidia-ml-dev

echo "Building project..."
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo "Build complete! Run with: ./build/blackbox-server"
```

Make executable and run:
```bash
chmod +x setup.sh
./setup.sh
```

