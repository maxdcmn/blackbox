# Blackbox

![License](https://img.shields.io/github/license/maxdcmn/blackbox)
![Status](https://img.shields.io/badge/status-active-green)

Blackbox is a monitoring solution for vLLM deployments, consisting of a GPU VRAM monitoring server and a terminal-based CLI client. It provides real-time insights into GPU memory utilization, KV cache blocks, process metrics, and Nsight Compute statistics.


- **blackbox-server**: C++ HTTP server that monitors GPU VRAM using NVML and Nsight Compute
- **blackbox-cli**: Go-based terminal client with interactive dashboard and JSON API

## blackbox-server

GPU VRAM monitoring server with NVML and Nsight Compute integration.

### Installation

**Prerequisites:** Linux (Ubuntu 22+), NVIDIA GPU with drivers 470+, CMake 3.15+, C++17 compiler

```bash
cd blackbox-server
./scripts/install_deps.sh
./scripts/setup.sh
./build/blackbox-server [port]
```

See [blackbox-server/docs/SETUP.md](blackbox-server/docs/SETUP.md) for detailed instructions.

### API Endpoints

**GET /vram** - Returns current VRAM metrics as JSON

```bash
curl http://localhost:6767/vram
```

**GET /vram/stream** - Server-Sent Events stream with real-time updates (~500ms interval)

```bash
curl -N http://localhost:6767/vram/stream
```

See [blackbox-server/docs/API.md](blackbox-server/docs/API.md) for complete API documentation.

## blackbox-cli

Terminal-based monitoring client with interactive dashboard and JSON output.

### Installation

```bash
git clone https://github.com/maxdcmn/blackbox.git
cd blackbox/blackbox-cli
go build -o blackbox ./main.go
sudo mv blackbox /usr/local/bin/
```

### Usage

**Interactive Dashboard:**
```bash
blackbox
```

**Command-Line Options:**
- `--url <url>`: Server URL (default: `http://127.0.0.1:8080`)
- `--endpoint <path>`: API endpoint (default: `/vram`)
- `--timeout <duration>`: HTTP timeout (default: `10s`)
- `--interval <duration>`: Polling interval for dashboard (default: `5s`)

**Examples:**
```bash
blackbox --url http://192.168.1.100:6767
blackbox stat --compact
blackbox stat --watch --interval 5s
```

### Configuration

Configuration file: `~/.config/blackbox/config.json`

```json
{
  "endpoints": [
    {
      "name": "local",
      "base_url": "http://127.0.0.1:8080",
      "endpoint": "/vram",
      "timeout": "2s"
    }
  ]
}
```


## API Response Structure

The `/vram` endpoint returns JSON with the following key fields:

- **Memory Metrics**: `total_bytes`, `used_bytes`, `free_bytes`, `reserved_bytes`, `used_percent`
- **Block Metrics**: `active_blocks`, `utilized_blocks`, `free_blocks`, `fragmentation_ratio`
- **Processes**: Array of GPU processes with PID, name, and memory usage
- **Blocks**: Array of memory blocks with allocation and utilization status
- **Nsight Metrics**: GPU activity metrics per process (occupancy, DRAM read/write, etc.)

**Data Sources:**
- **NVML**: System-level and process-level GPU memory
- **vLLM Metrics API**: KV cache block allocation and utilization
- **Nsight Compute**: GPU activity metrics

See [blackbox-server/docs/API.md](blackbox-server/docs/API.md) for complete field descriptions.

## Project Structure

```
blackbox/
├── blackbox-server/          # C++ HTTP server
│   ├── src/                  # C++ source code
│   ├── py_script/            # Python scripts (vLLM integration)
│   ├── scripts/              # Build and setup scripts
│   └── docs/                 # Documentation
│
└── blackbox-cli/             # Go CLI client
    ├── cmd/                  # Cobra commands
    └── internal/             # Client, config, UI components
```

## Further Documentation

- [Server API Reference](blackbox-server/docs/API.md) - Complete API documentation
- [Server Setup Guide](blackbox-server/docs/SETUP.md) - Detailed installation instructions
- [Server Implementation](blackbox-server/docs/IMPLEMENTATION.md) - Technical details
