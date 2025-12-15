# Setting Up GitHub Releases & APT Repository

## Step 1: Upload Package to GitHub Releases

1. **Create a release on GitHub:**
   ```bash
   # Tag the release
   git tag v1.0.0
   git push origin v1.0.0
   ```

2. **On GitHub:**
   - Go to: https://github.com/maxdcmn/blackbox/releases/new
   - Tag: `v1.0.0`
   - Title: `v1.0.0`
   - Upload: `blackbox-server/blackbox-server_1.0.0.deb` as an asset

3. **Direct download URL will be:**
   ```
   https://github.com/maxdcmn/blackbox/releases/download/v1.0.0/blackbox-server_1.0.0.deb
   ```

## Step 2: Simple Install (Direct Download)

**Users can install with:**
```bash
# One-liner
curl -sSL https://raw.githubusercontent.com/maxdcmn/blackbox/main/blackbox-server/scripts/install_from_github.sh | bash

# Or manually
wget https://github.com/maxdcmn/blackbox/releases/download/v1.0.0/blackbox-server_1.0.0.deb
sudo dpkg -i blackbox-server_1.0.0.deb
sudo apt-get install -f
```

## Step 3: Set Up APT Repository (Optional - for `apt install`)

### Option A: GitHub Pages APT Repo (Simplest)

1. **Create APT repo structure:**
   ```bash
   cd blackbox-server
   ./scripts/setup_apt_repo.sh
   ```

2. **Create gh-pages branch and push:**
   ```bash
   git checkout -b gh-pages
   git add apt-repo/
   git commit -m "Add APT repository"
   git push origin gh-pages
   ```

3. **Enable GitHub Pages:**
   - Go to: https://github.com/maxdcmn/blackbox/settings/pages
   - Source: `gh-pages` branch
   - Save

4. **Users add repo:**
   ```bash
   echo "deb [trusted=yes] https://maxdcmn.github.io/blackbox/apt-repo stable main" | sudo tee /etc/apt/sources.list.d/blackbox.list
   sudo apt update
   sudo apt install blackbox-server
   ```

### Option B: Direct GitHub Releases (No APT repo needed)

Just use the install script - it's simpler and works immediately.

## Quick Reference

**Download URL:**
```
https://github.com/maxdcmn/blackbox/releases/download/v1.0.0/blackbox-server_1.0.0.deb
```

**Install Script:**
```
https://raw.githubusercontent.com/maxdcmn/blackbox/main/blackbox-server/scripts/install_from_github.sh
```

**One-liner install:**
```bash
curl -sSL https://raw.githubusercontent.com/maxdcmn/blackbox/main/blackbox-server/scripts/install_from_github.sh | bash
```

