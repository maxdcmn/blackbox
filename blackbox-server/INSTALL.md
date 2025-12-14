# Installing Blackbox Server on a Remote VM

## Quick Install from GitHub (Simplest)

**One-liner install:**
```bash
curl -sSL https://raw.githubusercontent.com/maxdcmn/blackbox/main/blackbox-server/scripts/install_from_github.sh | bash
```

**Or manually:**
```bash
# Download and install
wget https://github.com/maxdcmn/blackbox/releases/download/v1.0.0/blackbox-server_1.0.0.deb
sudo dpkg -i blackbox-server_1.0.0.deb
sudo apt-get install -f
```

## Quick Install (Direct Download)

### Option 1: Host on GitHub Releases

1. **Upload to GitHub Releases:**
   - Create a release on GitHub
   - Upload `blackbox-server_1.0.0.deb` as an asset
   - Get the direct download URL (e.g., `https://github.com/user/repo/releases/download/v1.0.0/blackbox-server_1.0.0.deb`)

2. **On remote VM, download and install:**
```bash
# Download the package
wget https://github.com/user/repo/releases/download/v1.0.0/blackbox-server_1.0.0.deb

# Install
sudo apt update
sudo dpkg -i blackbox-server_1.0.0.deb
sudo apt-get install -f
```

### Option 2: Simple HTTP Server (from your local machine)

1. **Start a simple HTTP server:**
```bash
# On your local machine (in the directory with the .deb file)
cd /home/lethal365/blackbox/blackbox-server
python3 -m http.server 8000
```

2. **On remote VM, download:**
```bash
# Replace YOUR_LOCAL_IP with your machine's IP
wget http://YOUR_LOCAL_IP:8000/blackbox-server_1.0.0.deb

# Install
sudo apt update
sudo dpkg -i blackbox-server_1.0.0.deb
sudo apt-get install -f
```

### Option 3: Transfer via SCP (if you prefer)

```bash
# From your local machine
scp blackbox-server_1.0.0.deb user@remote-vm:/tmp/
```

### Step 3: Install dependencies and package

```bash
# Update package list
sudo apt update

# Install the package (will auto-install dependencies)
sudo dpkg -i /tmp/blackbox-server_1.0.0.deb

# Fix any missing dependencies
sudo apt-get install -f
```

### Step 4: Verify installation

```bash
# Check service status
sudo systemctl status blackbox-server

# Test the API
curl http://localhost:6767/vram

# Check if binary is in PATH
which blackbox-server
```

## Dependencies

The package automatically installs:
- `libboost-system` (1.74+)
- `curl`
- `python3`
- `python3-requests`

Optional (for full functionality):
- `nvidia-utils-535` (or 525/520) - for NVML support
- `nvidia-nsight-compute` - for detailed GPU metrics

## Service Management

```bash
# Start service
sudo systemctl start blackbox-server

# Stop service
sudo systemctl stop blackbox-server

# Enable on boot
sudo systemctl enable blackbox-server

# View logs
sudo journalctl -u blackbox-server -f
```

## Configuration

The server runs on port `6767` by default. To change this, edit the systemd service:

```bash
sudo systemctl edit --full blackbox-server
# Edit ExecStart line to change the port number
sudo systemctl daemon-reload
sudo systemctl restart blackbox-server
```

## Uninstall

```bash
sudo dpkg -r blackbox-server
```

