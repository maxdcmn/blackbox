# Blackbox

Blackbox is a comprehensive monitoring solution for vLLM and GPU VRAM usage. It provides real-time insights, timeseries data collection, and interactive web dashboards for tracking the performance and resource utilization of your vLLM deployments.

## Components Overview

1. **CLI Monitor** (`blackbox-cli/`) - Terminal-based dashboard with live metrics
2. **VRAM Monitor** (`blackbox-server/py_script/vram_monitor.py`) - Command-line timeseries tracker
3. **Web Dashboard** (`blackbox-dashboard/`) - **NEW!** Interactive web interface with graphs and database storage

---

## Web Dashboard (NEW!)

The web dashboard provides a modern, browser-based interface for monitoring GPU memory with interactive charts and historical data analysis.

### Quick Start

```bash
# 1. Install dependencies
cd blackbox-dashboard
pip install -r requirements.txt

# 2. Start the API server (includes embedded data collection)
python api.py

# 3. Open browser to http://localhost:8001/
```

**Note:** Data collection is now embedded in the API server. When you add a node via the API, it automatically starts collecting data. No need to run a separate `data_collector.py` process!

### Features

- 📊 **Interactive Charts**: Real-time graphs for memory usage, processes, and fragmentation
- 💾 **Database Storage**: SQLite/PostgreSQL support for historical data
- 🔄 **Auto-refresh**: Configurable intervals (5s, 10s, 30s, 1m)
- ⏱️ **Time Ranges**: View data from 5 minutes to 24 hours
- 🔍 **Process Tracking**: Monitor individual GPU processes
- 📈 **Timeseries API**: REST endpoints for programmatic access
- 🎨 **Modern UI**: Gradient-styled responsive interface

See [`blackbox-dashboard/README.md`](blackbox-dashboard/README.md) for detailed documentation.

---

## Components

### VRAM Monitor (Python)

The VRAM Monitor (`blackbox-server/py_script/vram_monitor.py`) tracks GPU memory usage over time with timeseries data collection.

#### Features:
- Polls VRAM API every 5 seconds (configurable)
- Detects new processes, blocks, and threads by tracking IDs
- Stores timeseries history in JSON format (~/.blackbox_vram_history.json)
- Shows real-time statistics and trends
- Highlights new activity as it's detected
- Export history to CSV for analysis

#### Usage:

```bash
# Start monitoring with default settings (polls every 5 seconds)
./blackbox-server/py_script/vram_monitor.py

# Custom polling interval (e.g., 10 seconds)
./blackbox-server/py_script/vram_monitor.py --interval 10

# Custom API endpoint
./blackbox-server/py_script/vram_monitor.py --url http://localhost:8080/vram

# Custom history file location
./blackbox-server/py_script/vram_monitor.py --history-file /path/to/history.json

# Export history to CSV
./blackbox-server/py_script/vram_monitor.py --export-csv vram_history.csv

# Show statistics from collected history
./blackbox-server/py_script/vram_monitor.py --stats
```

#### Tracked Metrics:
- `total_bytes`, `used_bytes`, `free_bytes`, `reserved_bytes`
- `used_percent` - percentage of memory used
- `active_blocks`, `free_blocks` - memory block counts
- `atomic_allocations_bytes` - atomic allocation size
- `fragmentation_ratio` - memory fragmentation metric
- `processes` - running processes with memory usage
- `threads` - active threads with allocations
- `blocks` - detailed block information
- `nsight_metrics` - NVIDIA Nsight profiling data
- `vllm_metrics` - vLLM-specific metrics

#### New Request Detection:
The monitor tracks process IDs, block IDs, and thread IDs to detect new activity:
- **New Processes**: Highlights when new processes start using GPU memory
- **New Blocks**: Shows when new memory blocks are allocated
- **New Threads**: Tracks when new threads are created

#### History & Timeseries:
- Automatically saves snapshots to `~/.blackbox_vram_history.json`
- Maintains last 1000 snapshots
- Calculates trends (memory delta, process count changes, fragmentation)
- Shows statistics on exit (average, min, max usage)

---

## API Data Structure


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
