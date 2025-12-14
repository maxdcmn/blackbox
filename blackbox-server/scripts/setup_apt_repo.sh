#!/bin/bash
# Setup script to create a simple APT repository on GitHub Pages
# This allows: apt install blackbox-server

set -e

REPO="maxdcmn/blackbox"
VERSION="1.0.0"
DEB_FILE="blackbox-server_${VERSION}.deb"

echo "Setting up APT repository on GitHub Pages..."

# Create repo structure
mkdir -p apt-repo/pool/main/b/blackbox-server
mkdir -p apt-repo/dists/stable/main/binary-amd64

# Copy the .deb file
cp ${DEB_FILE} apt-repo/pool/main/b/blackbox-server/

# Create Packages file
cd apt-repo
dpkg-scanpackages --arch amd64 pool/ > dists/stable/main/binary-amd64/Packages
gzip -k dists/stable/main/binary-amd64/Packages

# Create Release file
cat > dists/stable/Release << EOF
Origin: Blackbox
Label: Blackbox Repository
Suite: stable
Codename: stable
Architectures: amd64
Components: main
Description: Blackbox Server APT Repository
Date: $(date -Ru)
EOF

# Sign Release (optional, requires GPG)
# gpg -abs -o dists/stable/Release.gpg dists/stable/Release

echo "✓ Repository structure created in apt-repo/"
echo ""
echo "Next steps:"
echo "1. Commit and push apt-repo/ to gh-pages branch"
echo "2. Enable GitHub Pages for the repository"
echo "3. Users can then add:"
echo "   echo 'deb [trusted=yes] https://${REPO//\//.github.io/}/apt-repo stable main' | sudo tee /etc/apt/sources.list.d/blackbox.list"
echo "   sudo apt update && sudo apt install blackbox-server"

