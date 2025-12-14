#!/bin/bash
set -e

# Configuration
PACKAGE_NAME="blackbox-server"
VERSION="1.0.0"
ARCH="amd64"
MAINTAINER="Blackbox Team <blackbox@example.com>"

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building Blackbox Server Debian Package (Manual Method)${NC}"

# Step 1: Build the binary
echo -e "\n${YELLOW}Step 1: Building binary...${NC}"
cd "$(dirname "$0")/.."
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

if [ ! -f "blackbox-server" ]; then
    echo "Error: Binary not found after build!"
    exit 1
fi

echo -e "${GREEN}✓ Binary built successfully${NC}"

# Step 2: Create package directory structure
echo -e "\n${YELLOW}Step 2: Creating package structure...${NC}"
cd ..
PACKAGE_DIR="${PACKAGE_NAME}_${VERSION}"
rm -rf "$PACKAGE_DIR" "${PACKAGE_DIR}.deb"

mkdir -p "$PACKAGE_DIR/DEBIAN"
mkdir -p "$PACKAGE_DIR/usr/local/bin"
mkdir -p "$PACKAGE_DIR/usr/lib/blackbox-server"
mkdir -p "$PACKAGE_DIR/lib/systemd/system"

echo -e "${GREEN}✓ Directory structure created${NC}"

# Step 3: Copy files
echo -e "\n${YELLOW}Step 3: Copying files...${NC}"

# Copy binary
cp build/blackbox-server "$PACKAGE_DIR/usr/local/bin/"
chmod 0755 "$PACKAGE_DIR/usr/local/bin/blackbox-server"

# Copy Python scripts
cp py_script/parse_vram.py "$PACKAGE_DIR/usr/lib/blackbox-server/"
cp py_script/chat_qwen.py "$PACKAGE_DIR/usr/lib/blackbox-server/"
chmod 0755 "$PACKAGE_DIR/usr/lib/blackbox-server/parse_vram.py"
chmod 0755 "$PACKAGE_DIR/usr/lib/blackbox-server/chat_qwen.py"

# Create symlinks for Python scripts
ln -s /usr/lib/blackbox-server/parse_vram.py "$PACKAGE_DIR/usr/local/bin/parse-vram"
ln -s /usr/lib/blackbox-server/chat_qwen.py "$PACKAGE_DIR/usr/local/bin/chat-qwen"

# Copy systemd service
cp debian/blackbox-server.service "$PACKAGE_DIR/lib/systemd/system/"

echo -e "${GREEN}✓ Files copied${NC}"

# Step 4: Create control file
echo -e "\n${YELLOW}Step 4: Creating control file...${NC}"

# Calculate installed size
INSTALLED_SIZE=$(du -sk "$PACKAGE_DIR" | cut -f1)

cat > "$PACKAGE_DIR/DEBIAN/control" << EOF
Package: ${PACKAGE_NAME}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH}
Maintainer: ${MAINTAINER}
Installed-Size: ${INSTALLED_SIZE}
Depends: libboost-system1.84.0 | libboost-system1.83.0 | libboost-system1.82.0 | libboost-system1.81.0 | libboost-system1.80.0 | libboost-system1.74.0, curl, python3, python3-requests
Recommends: nvidia-utils-535 | nvidia-utils-525 | nvidia-utils-520, nvidia-nsight-compute
Description: GPU VRAM monitoring server for vLLM deployments
 Blackbox Server provides real-time GPU VRAM monitoring with NVML and
 Nsight Compute integration. It exposes RESTful APIs for querying GPU
 memory usage, process-level allocations, and block-level utilization
 metrics for vLLM inference servers.
EOF

echo -e "${GREEN}✓ Control file created${NC}"

# Step 5: Create postinst script
echo -e "\n${YELLOW}Step 5: Creating post-install script...${NC}"

cat > "$PACKAGE_DIR/DEBIAN/postinst" << 'POSTINST'
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

chmod 0755 "$PACKAGE_DIR/DEBIAN/postinst"

# Step 6: Create prerm script
echo -e "\n${YELLOW}Step 6: Creating pre-remove script...${NC}"

cat > "$PACKAGE_DIR/DEBIAN/prerm" << 'PRERM'
#!/bin/bash
set -e

# Stop and disable systemd service
if [ -d /run/systemd/system ]; then
    systemctl stop blackbox-server.service || true
    systemctl disable blackbox-server.service || true
fi

exit 0
PRERM

chmod 0755 "$PACKAGE_DIR/DEBIAN/prerm"

echo -e "${GREEN}✓ Scripts created${NC}"

# Step 7: Build the .deb package
echo -e "\n${YELLOW}Step 7: Building .deb package...${NC}"

dpkg-deb --build "$PACKAGE_DIR"

if [ -f "${PACKAGE_DIR}.deb" ]; then
    echo -e "\n${GREEN}✓ Package built successfully: ${PACKAGE_DIR}.deb${NC}"
    echo -e "\n${GREEN}Package size:$(du -h ${PACKAGE_DIR}.deb | cut -f1)${NC}"
    echo -e "\n${GREEN}Install with: sudo dpkg -i ${PACKAGE_DIR}.deb${NC}"
else
    echo "Error: Package file not created!"
    exit 1
fi


