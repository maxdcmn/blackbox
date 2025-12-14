#!/bin/bash
# Simple install script for blackbox-server from GitHub Releases
set -e

REPO="maxdcmn/blackbox"
VERSION="1.0.0"
DEB_FILE="blackbox-server_${VERSION}.deb"
GITHUB_URL="https://github.com/${REPO}/releases/download/v${VERSION}/${DEB_FILE}"

# Alternative: if package is in blackbox-server/ subdirectory
# GITHUB_URL="https://github.com/${REPO}/releases/download/v${VERSION}/blackbox-server/${DEB_FILE}"

echo "Installing blackbox-server from GitHub..."
echo "Downloading: ${GITHUB_URL}"

# Download the package
wget -O /tmp/${DEB_FILE} ${GITHUB_URL} || {
    echo "Error: Failed to download package"
    exit 1
}

# Install
sudo dpkg -i /tmp/${DEB_FILE} || {
    echo "Fixing dependencies..."
    sudo apt-get install -f -y
}

# Cleanup
rm -f /tmp/${DEB_FILE}

echo "✓ blackbox-server installed successfully!"
echo "Service status: sudo systemctl status blackbox-server"

