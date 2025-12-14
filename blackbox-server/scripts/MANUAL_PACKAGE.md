# Manual Debian Package Creation

This guide shows how to create a `.deb` package manually using `dpkg-deb`.

## Quick Build (Automated)

```bash
cd blackbox-server
./scripts/build_deb_manual.sh
```

## Manual Steps

### Step 1: Build the Binary

```bash
cd blackbox-server
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..
```

### Step 2: Create Package Directory Structure

```bash
# Create main package directory
mkdir -p blackbox-server_1.0.0/DEBIAN

# Create destination directories
mkdir -p blackbox-server_1.0.0/usr/local/bin
mkdir -p blackbox-server_1.0.0/usr/lib/blackbox-server
mkdir -p blackbox-server_1.0.0/lib/systemd/system
```

### Step 3: Copy Files

```bash
# Copy binary
cp build/blackbox-server blackbox-server_1.0.0/usr/local/bin/
chmod 0755 blackbox-server_1.0.0/usr/local/bin/blackbox-server

# Copy Python scripts
cp py_script/parse_vram.py blackbox-server_1.0.0/usr/lib/blackbox-server/
cp py_script/chat_qwen.py blackbox-server_1.0.0/usr/lib/blackbox-server/
chmod 0755 blackbox-server_1.0.0/usr/lib/blackbox-server/*.py

# Create symlinks for CLI commands
ln -s /usr/lib/blackbox-server/parse_vram.py blackbox-server_1.0.0/usr/local/bin/parse-vram
ln -s /usr/lib/blackbox-server/chat_qwen.py blackbox-server_1.0.0/usr/local/bin/chat-qwen

# Copy systemd service
cp debian/blackbox-server.service blackbox-server_1.0.0/lib/systemd/system/
```

### Step 4: Create Control File

Create `blackbox-server_1.0.0/DEBIAN/control`:

```bash
cat > blackbox-server_1.0.0/DEBIAN/control << 'EOF'
Package: blackbox-server
Version: 1.0.0
Section: utils
Priority: optional
Architecture: amd64
Maintainer: Blackbox Team <blackbox@example.com>
Installed-Size: 2048
Depends: libboost-system1.84.0 | libboost-system1.83.0 | libboost-system1.82.0 | libboost-system1.81.0 | libboost-system1.80.0 | libboost-system1.74.0, libabseil20240116, curl, python3, python3-requests
Recommends: nvidia-utils-535 | nvidia-utils-525 | nvidia-utils-520, nvidia-nsight-compute
Description: GPU VRAM monitoring server for vLLM deployments
 Blackbox Server provides real-time GPU VRAM monitoring with NVML and
 Nsight Compute integration. It exposes RESTful APIs for querying GPU
 memory usage, process-level allocations, and block-level utilization
 metrics for vLLM inference servers.
EOF
```

**Note:** Replace `Installed-Size: 2048` with actual size:
```bash
du -sk blackbox-server_1.0.0 | cut -f1
```

### Step 5: Create Post-Install Script (Optional)

Create `blackbox-server_1.0.0/DEBIAN/postinst`:

```bash
cat > blackbox-server_1.0.0/DEBIAN/postinst << 'POSTINST'
#!/bin/bash
set -e

# Enable and start systemd service
if [ -d /run/systemd/system ]; then
    systemctl daemon-reload
    systemctl enable blackbox-server.service || true
    systemctl start blackbox-server.service || true
fi

exit 0
POSTINST

chmod 0755 blackbox-server_1.0.0/DEBIAN/postinst
```

### Step 6: Create Pre-Remove Script (Optional)

Create `blackbox-server_1.0.0/DEBIAN/prerm`:

```bash
cat > blackbox-server_1.0.0/DEBIAN/prerm << 'PRERM'
#!/bin/bash
set -e

# Stop and disable systemd service
if [ -d /run/systemd/system ]; then
    systemctl stop blackbox-server.service || true
    systemctl disable blackbox-server.service || true
fi

exit 0
PRERM

chmod 0755 blackbox-server_1.0.0/DEBIAN/prerm
```

### Step 7: Build the .deb Package

```bash
dpkg-deb --build blackbox-server_1.0.0
```

This creates `blackbox-server_1.0.0.deb`.

### Step 8: Install and Test

```bash
# Install
sudo dpkg -i blackbox-server_1.0.0.deb

# Fix any missing dependencies
sudo apt-get install -f

# Verify installation
which blackbox-server
which parse-vram
systemctl status blackbox-server
```

## Package Structure

```
blackbox-server_1.0.0/
├── DEBIAN/
│   ├── control          # Package metadata
│   ├── postinst         # Post-install script
│   └── prerm            # Pre-remove script
├── usr/
│   ├── local/
│   │   └── bin/
│   │       ├── blackbox-server  # Main binary
│   │       ├── parse-vram       # Symlink
│   │       └── chat-qwen        # Symlink
│   └── lib/
│       └── blackbox-server/
│           ├── parse_vram.py
│           └── chat_qwen.py
└── lib/
    └── systemd/
        └── system/
            └── blackbox-server.service
```

## Verification

```bash
# View package contents
dpkg -c blackbox-server_1.0.0.deb

# View package info
dpkg -I blackbox-server_1.0.0.deb

# Check package integrity
dpkg-deb -c blackbox-server_1.0.0.deb
```


