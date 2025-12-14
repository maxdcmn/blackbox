# Publishing Blackbox Server as a Debian Package

## Building the Package

### Prerequisites

```bash
sudo apt update
sudo apt install -y devscripts debhelper build-essential cmake \
    libboost-dev libboost-system-dev libabseil-dev pkg-config \
    dpkg-dev
```

**Note:** For `dpkg-buildpackage`, `libabseil-dev` is required. The manual build script (`./scripts/build_deb_manual.sh`) doesn't need it as it allows FetchContent downloads.

### Build Methods

#### Method 1: Manual Build Script (Recommended for Quick Builds)

**Simplest method** - uses the provided script:

```bash
cd blackbox-server
./scripts/build_deb_manual.sh
```

This creates:
- `blackbox-server_1.0.0.deb` - The installable package (in current directory)

**Pros:**
- No additional Debian tools needed
- Fast and simple
- Works without full Debian packaging setup

#### Method 2: Standard Debian Build (dpkg-buildpackage)

**For official/repository builds:**

1. **Navigate to the source directory:**
```bash
cd blackbox-server
```

2. **Clean any previous builds:**
```bash
rm -rf build debian/blackbox-server debian/files debian/*.substvars
```

3. **Build the package:**
```bash
dpkg-buildpackage -us -uc
```

This creates:
- `../blackbox-server_1.0.0-1_amd64.deb` - The installable package
- `../blackbox-server_1.0.0-1_amd64.changes` - Change log for upload
- `../blackbox-server_1.0.0-1.dsc` - Source package description
- `../blackbox-server_1.0.0-1.tar.xz` - Source tarball

**Build with Signing (for repository publishing):**
```bash
dpkg-buildpackage -kYOUR_GPG_KEY_ID
```

**Pros:**
- Standard Debian workflow
- Creates source package for distribution
- Better for repository publishing

## Publishing Options

### Option 1: Direct .deb Distribution

**Simplest method** - distribute the `.deb` file directly:

```bash
# Build the package (choose one method)
./scripts/build_deb_manual.sh              # Creates: blackbox-server_1.0.0.deb
# OR
dpkg-buildpackage -us -uc                   # Creates: ../blackbox-server_1.0.0-1_amd64.deb

# Install on target system
sudo dpkg -i blackbox-server_1.0.0.deb      # Manual build
# OR
sudo dpkg -i ../blackbox-server_1.0.0-1_amd64.deb  # Standard build

# Fix any missing dependencies
sudo apt-get install -f
```

**Pros:**
- Simple, no repository setup needed
- Works for internal/private distribution

**Cons:**
- No automatic dependency resolution
- No version management
- Manual updates required

### Option 2: Private APT Repository

**Best for internal/team distribution:**

#### Setup Repository Server

1. **Install required tools:**
```bash
sudo apt install -y reprepro apache2
```

2. **Create repository structure:**
```bash
sudo mkdir -p /var/www/repos/blackbox/{conf,dists,pool}
cd /var/www/repos/blackbox
```

3. **Create `conf/distributions`:**
```conf
Origin: Blackbox
Label: Blackbox Repository
Codename: jammy
Architectures: amd64
Components: main
Description: Blackbox Server Debian Repository
SignWith: YOUR_GPG_KEY_ID
```

4. **Add packages:**
```bash
reprepro includedeb jammy /path/to/blackbox-server_1.0.0-1_amd64.deb
```

5. **Export repository:**
```bash
reprepro export jammy
```

6. **Configure Apache:**
```apache
<VirtualHost *:80>
    ServerName repos.yourdomain.com
    DocumentRoot /var/www/repos/blackbox
    
    <Directory /var/www/repos/blackbox>
        Options Indexes FollowSymLinks
        AllowOverride None
        Require all granted
    </Directory>
</VirtualHost>
```

#### Client Setup

1. **Add GPG key:**
```bash
wget -qO - https://repos.yourdomain.com/KEY.gpg | sudo apt-key add -
```

2. **Add repository:**
```bash
echo "deb http://repos.yourdomain.com/blackbox jammy main" | \
    sudo tee /etc/apt/sources.list.d/blackbox.list
```

3. **Install:**
```bash
sudo apt update
sudo apt install blackbox-server
```

### Option 3: Launchpad PPA (Ubuntu)

**For public Ubuntu distribution:**

1. **Create Launchpad account** at https://launchpad.net

2. **Upload package:**
```bash
# Build source package
debuild -S

# Upload to PPA
dput ppa:yourusername/blackbox blackbox-server_1.0.0-1_source.changes
```

3. **Users install:**
```bash
sudo add-apt-repository ppa:yourusername/blackbox
sudo apt update
sudo apt install blackbox-server
```

### Option 4: GitHub Releases

**Simple distribution via GitHub:**

1. **Build package:**
```bash
dpkg-buildpackage -us -uc
```

2. **Create GitHub release:**
```bash
gh release create v1.0.0 ../blackbox-server_1.0.0-1_amd64.deb \
    --title "Blackbox Server v1.0.0" \
    --notes "Initial release"
```

3. **Users download and install:**
```bash
wget https://github.com/yourorg/blackbox/releases/download/v1.0.0/blackbox-server_1.0.0-1_amd64.deb
sudo dpkg -i blackbox-server_1.0.0-1_amd64.deb
sudo apt-get install -f
```

## Version Management

### Update Version

1. **Edit `debian/changelog`:**
```bash
dch -i
```

Or manually:
```
blackbox-server (1.0.1-1) unstable; urgency=medium

  * Bug fixes and improvements

 -- Your Name <email@example.com>  Mon, 23 Dec 2024 12:00:00 +0000
```

2. **Rebuild:**
```bash
dpkg-buildpackage -us -uc
```

## Testing Before Publishing

### Test Installation

```bash
# Build package
dpkg-buildpackage -us -uc

# Test in clean environment (using Docker)
docker run -it --rm ubuntu:22.04 bash
apt update
apt install -y /path/to/blackbox-server_1.0.0-1_amd64.deb
```

### Verify Package Contents

```bash
dpkg -c ../blackbox-server_1.0.0-1_amd64.deb
dpkg -I ../blackbox-server_1.0.0-1_amd64.deb
```

## Recommended Workflow

1. **Development:**
   - Make changes
   - Test locally
   - Update `debian/changelog`

2. **Build:**
   ```bash
   dpkg-buildpackage -us -uc
   ```

3. **Test:**
   ```bash
   sudo dpkg -i ../blackbox-server_1.0.0-1_amd64.deb
   sudo systemctl status blackbox-server
   ```

4. **Publish:**
   - For internal: Use private repository (Option 2)
   - For public: Use GitHub Releases (Option 4) or Launchpad PPA (Option 3)

## Troubleshooting

### Build Errors

```bash
# Clean and rebuild
debian/rules clean
dpkg-buildpackage -us -uc
```

### Missing Dependencies

Check `debian/control` and ensure all build dependencies are listed in `Build-Depends`.

### GPG Signing Issues

```bash
# Generate GPG key
gpg --gen-key

# Export public key
gpg --armor --export YOUR_KEY_ID > KEY.gpg
```

