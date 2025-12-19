# Blackbox

![License](https://img.shields.io/github/license/maxdcmn/blackbox)
![Status](https://img.shields.io/badge/status-active-green)

Blackbox is a monitoring and management solution for vLLM deployments, consisting of a GPU VRAM monitoring server and a terminal-based CLI client. It provides real-time insights into GPU memory utilization, KV cache blocks, process metrics and Nsight Compute statistics, along with model deployment and optimization capabilities.

## Components

- **blackbox-server**: C++ HTTP server that monitors GPU VRAM using NVML and Nsight Compute, with model deployment capabilities
- **blackbox-cli**: Go-based terminal client with interactive dashboard, JSON output, and model management commands

## Quick Start

### Building the Server

From the project root:

```bash
# Build the server
make

# Run the server (default port: 6767)
make run

# Or manually specify a port
./blackbox-server/build/blackbox-server 6767
```

### Building the CLI

```bash
cd blackbox-cli
go build -o blackbox ./main.go
sudo mv blackbox /usr/local/bin/
```

### Configuration

Copy the example environment file and configure:

```bash
cp env.example .env
# Edit .env with your values (HF_TOKEN, BLACKBOX_SERVER_URL, etc.)
```

**Required (for model deployment):**
- `HF_TOKEN` - HuggingFace API token (get from https://huggingface.co/settings/tokens)

**Optional:**
- `BLACKBOX_SERVER_URL` - Server URL (default: `http://localhost:6767`)
- `MAX_CONCURRENT_MODELS` - Maximum concurrent models (default: 3)
- `GPU_TYPE` - GPU type override (T4, A100, H100, L40) or leave empty for auto-detection

## blackbox-server

GPU VRAM monitoring server with NVML and Nsight Compute integration, plus model deployment management.

### Installation

**Prerequisites:** Linux (Ubuntu 22+), NVIDIA GPU with drivers 470+, CMake 3.15+, C++17 compiler, Docker

```bash
# Build from project root
make
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

**POST /deploy** - Deploy a HuggingFace model using vLLM Docker

```bash
curl -X POST http://localhost:6767/deploy \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "Qwen/Qwen2.5-7B-Instruct",
    "hf_token": "hf_xxxxxxxxxxxxx",
    "port": 8000
  }'
```

**POST /spindown** - Stop and remove a deployed model

```bash
curl -X POST http://localhost:6767/spindown \
  -H "Content-Type: application/json" \
  -d '{"model_id": "Qwen/Qwen2.5-7B-Instruct"}'
```

**GET /models** - List all deployed models and their status

```bash
curl http://localhost:6767/models
```

**POST /optimize** - Optimize GPU utilization by restarting overallocated models

```bash
curl -X POST http://localhost:6767/optimize
```

See [blackbox-server/docs/API.md](blackbox-server/docs/API.md) for complete API documentation.

## blackbox-cli

Terminal-based monitoring client with interactive dashboard, JSON output, and model management.

### Installation

```bash
git clone https://github.com/maxdcmn/blackbox.git
cd blackbox/blackbox-cli
go build -o blackbox ./main.go
sudo mv blackbox /usr/local/bin/
```

### Usage

#### Commands

| Command | Description |
|---------|-------------|
| `blackbox` | Launch interactive dashboard with real-time VRAM metrics, charts, and model management |
| `blackbox stat` | Print current VRAM snapshot as JSON |
| `blackbox stat --watch` | Continuously watch and print snapshots |
| `blackbox stream` | Stream real-time metrics via Server-Sent Events |
| `blackbox models` | List all deployed models and their status |
| `blackbox spindown <model_id>` | Stop and remove a deployed model |
| `blackbox optimize` | Optimize GPU utilization by restarting overallocated models |

#### Global Options

| Option | Description | Default |
|--------|-------------|---------|
| `--url <url>` | Server URL | `http://127.0.0.1:6767` |
| `--endpoint <path>` | API endpoint path | `/vram` |
| `--timeout <duration>` | HTTP request timeout | `10s` |
| `--interval <duration>` | Polling interval (dashboard/watch) | `3s` |
| `--debug` | Enable debug logging | `false` |
| `--log-file <path>` | Write logs to file | stderr |

#### Examples

```bash
# Interactive dashboard
blackbox

# Connect to remote server
blackbox --url http://192.168.1.100:6767

# Get one-time snapshot
blackbox stat

# Watch metrics continuously
blackbox stat --watch --interval 5s

# Stream real-time updates
blackbox stream

# Model management
blackbox models
blackbox spindown Qwen/Qwen2.5-7B-Instruct
blackbox optimize
```

### Configuration

Configuration file: `~/.config/blackbox/config.json`

```json
{
  "endpoints": [
    {
      "name": "local",
      "base_url": "http://127.0.0.1:6767",
      "endpoint": "/vram",
      "timeout": "2s"
    }
  ]
}
```


## API Response Structure

The `/vram` endpoint returns JSON with the following key fields:

- **Memory Metrics**: `total_bytes`, `used_bytes`, `free_bytes`, `reserved_bytes`, `used_percent`
- **Block Metrics**: `allocated_blocks`, `utilized_blocks`, `free_blocks`, `fragmentation_ratio`
- **Processes**: Array of GPU processes with PID, name, and memory usage
- **Blocks**: Array of memory blocks with allocation and utilization status (each block has a `size` field in bytes)
- **Nsight Metrics**: GPU activity metrics per process (occupancy, DRAM read/write, atomic operations, etc.)

**Data Sources:**
- **NVML (NVIDIA Management Library)**: System-level and process-level GPU memory
- **vLLM Metrics API**: KV cache block allocation and utilization (`allocated_blocks`, `kv_cache_usage_perc`)
- **Nsight Compute (NCU)**: GPU activity metrics (occupancy, DRAM throughput, atomic operations)
- **Calculated Fields**: `free_blocks`, `fragmentation_ratio`, `block.size` (calculated from process memory and block count)

**Key Metrics:**
- **`allocated_blocks`**: Total blocks vLLM has allocated for KV cache
- **`utilized_blocks`**: Count of blocks actively storing data (calculated from vLLM's `kv_cache_usage_perc`)
- **`free_blocks`**: Allocated but unused blocks = `allocated_blocks - utilized_blocks`
- **Block size**: Calculated dynamically as `process_gpu_memory_bytes / num_allocated_blocks`

See [blackbox-server/docs/API.md](blackbox-server/docs/API.md) for complete field descriptions and examples.

## Project Structure

```
blackbox/
├── blackbox-server/          # C++ HTTP server
│   ├── src/                  # C++ source code
│   │   ├── infra/            # HTTP server implementation
│   │   ├── services/         # Core services (VRAM tracking, deployment, optimization)
│   │   ├── utils/            # Logging, env utilities
│   │   └── configs/          # GPU-specific configuration files
│   ├── include/              # Header files
│   ├── docs/                 # Documentation (API, Setup, Implementation)
│   └── CMakeLists.txt        # Build configuration
│
├── blackbox-cli/             # Go CLI client
│   ├── cmd/                  # CLI commands
│   ├── internal/
│   │   ├── client/           # HTTP client for server API
│   │   ├── config/           # Configuration management
│   │   ├── model/            # Data models
│   │   ├── ui/               # Interactive dashboard components
│   │   └── utils/            # Logging utilities
│   └── main.go               # Entry point
│
├── Makefile                  # Build automation
└── env.example               # Environment configuration template
```

## Build System

The project includes a Makefile for easy building:

```bash
make          # Build blackbox-server (default)
make clean    # Remove build artifacts
make install  # Install server to /usr/local/bin
make run      # Build and run server on port 6767
make help     # Show help
```

## Further Documentation

- [Server API Reference](blackbox-server/docs/API.md) - Complete API documentation with examples
- [Server Setup Guide](blackbox-server/docs/SETUP.md) - Detailed installation and configuration instructions
- [Server Implementation](blackbox-server/docs/IMPLEMENTATION.md) - Technical architecture and implementation details
- [Server README](blackbox-server/README.md) - Server-specific documentation
