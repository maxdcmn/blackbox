# Blackbox Server Documentation

Complete documentation for the Blackbox VRAM monitoring server.

## Table of Contents

- [Overview](#overview)
- [Setup Guide](#setup-guide)
- [Architecture](#architecture)
- [API Reference](#api-reference)
- [Integration Guide](#integration-guide)
- [Implementation Details](#implementation-details)
- [Troubleshooting](#troubleshooting)

---

## Overview

Blackbox Server is a high-performance C++ HTTP server that provides real-time GPU VRAM monitoring and metrics. It uses NVIDIA Management Library (NVML) for system-level GPU monitoring and NVIDIA Nsight Compute for detailed GPU performance profiling.

### Key Features

- **Real-time VRAM Monitoring**: Track total, used, free, and reserved GPU memory
- **Process-level Metrics**: Monitor VRAM usage per process (PID)
- **Nsight Compute Integration**: Detailed GPU performance metrics including:
  - Atomic operations count
  - Threads per block
  - GPU occupancy
  - Active CUDA blocks
  - Memory throughput (DRAM read/write)
- **Streaming Support**: Server-Sent Events (SSE) for real-time updates
- **Lightweight**: Minimal dependencies, fast response times

### Technology Stack

- **C++17**: Core language
- **Boost.Asio/Beast**: HTTP server and networking
- **NVML**: NVIDIA Management Library for GPU monitoring
- **Nsight Compute (NCU)**: GPU profiling (optional)
- **Abseil**: String utilities
- **CMake**: Build system

---

## Setup Guide

### Prerequisites

1. **NVIDIA GPU** with supported drivers
2. **Linux** (Ubuntu/Debian recommended)
3. **C++17 compatible compiler** (GCC 7+ or Clang 5+)
4. **CMake 3.15+**

### Quick Start

```bash
# 1. Install dependencies
./scripts/install_deps.sh

# 2. Build the server
./scripts/setup.sh

# 3. Run the server
./build/blackbox-server

# 4. Test the endpoint
curl http://localhost:6767/vram
```

### Detailed Installation

See [SETUP.md](../SETUP.md) for complete installation instructions.

### Dependencies

The server automatically downloads and builds:
- **Boost 1.84.0** (system library only)
- **Abseil C++** (20240116.2)

Optional dependencies:
- **NVML** (`libnvidia-ml-dev` or `cuda-nvml-dev-*`)
- **Nsight Compute** (for advanced profiling)

---

## Architecture

### System Components

```
┌─────────────────┐
│  HTTP Client    │
└────────┬────────┘
         │ HTTP/SSE
         ▼
┌─────────────────┐
│  Boost.Beast    │  HTTP Server
│  HTTP Handler   │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  VRAM Monitor   │  Core Logic
│  (main.cpp)     │
└────────┬────────┘
         │
    ┌────┴────┐
    ▼         ▼
┌────────┐ ┌──────────────┐
│  NVML  │ │ Nsight       │
│  API   │ │ Compute (ncu)│
└────────┘ └──────────────┘
```

### Data Flow

1. **Client Request** → HTTP GET `/vram` or `/vram/stream`
2. **Server Processing**:
   - Initialize NVML (if available)
   - Query GPU memory info
   - Enumerate running GPU processes
   - Collect Nsight Compute metrics (if available)
   - Aggregate data into JSON response
3. **Response** → JSON payload with VRAM metrics

### Key Data Structures

#### `DetailedVRAMInfo`
```cpp
struct DetailedVRAMInfo {
    unsigned long long total;           // Total GPU memory (bytes)
    unsigned long long used;            // Used GPU memory (bytes)
    unsigned long long free;            // Free GPU memory (bytes)
    unsigned long long reserved;        // Reserved memory (bytes)
    std::vector<MemoryBlock> blocks;    // Memory blocks
    std::vector<ProcessMemory> processes; // GPU processes
    std::vector<ThreadInfo> threads;    // Thread info
    unsigned int allocated_blocks;     // Allocated memory blocks
    unsigned int utilized_blocks;      // Utilized memory blocks
    unsigned int free_blocks;           // Free memory blocks
    unsigned long long atomic_allocations; // Total allocations
    double fragmentation_ratio;         // Memory fragmentation
    std::map<unsigned int, NsightMetrics> nsight_metrics; // Per-PID metrics
};
```

#### `NsightMetrics`
```cpp
struct NsightMetrics {
    unsigned long long atomic_operations;  // Atomic ops count
    unsigned long long threads_per_block;  // CUDA threads per block
    double occupancy;                      // GPU occupancy (%)
    unsigned long long active_blocks;      // Active CUDA blocks
    unsigned long long memory_throughput;  // Memory throughput (bytes/sec)
    unsigned long long dram_read_bytes;    // DRAM read bytes
    unsigned long long dram_write_bytes;   // DRAM write bytes
    bool available;                        // Metrics available
};
```

---

## API Reference

### Endpoints

#### `GET /vram`

Returns current VRAM metrics as JSON.

**Response Format:**
```json
{
  "total_bytes": 34359738368,
  "used_bytes": 8589934592,
  "free_bytes": 25769803776,
  "reserved_bytes": 8589934592,
  "used_percent": 25.00,
  "allocated_blocks": 1,
  "utilized_blocks": 0,
  "free_blocks": 0,
  "atomic_allocations_bytes": 8589934592,
  "fragmentation_ratio": 0.25,
  "processes": [
    {
      "pid": 12345,
      "name": "python",
      "used_bytes": 8589934592,
      "reserved_bytes": 8589934592
    }
  ],
  "threads": [
    {
      "thread_id": 0,
      "allocated_bytes": 8589934592,
      "state": "active"
    }
  ],
  "blocks": [
    {
      "block_id": 0,
      "address": 0,
      "size": 16384,
      "type": "other",
      "allocated": true,
      "utilized": true
    }
  ],
  "nsight_metrics": {
    "12345": {
      "atomic_operations": 0,
      "threads_per_block": 0,
      "occupancy": 0.0,
      "active_blocks": 0,
      "memory_throughput": 0,
      "dram_read_bytes": 0,
      "dram_write_bytes": 0,
      "available": false
    }
  }
}
```

**Response Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `total_bytes` | number | Total GPU memory in bytes |
| `used_bytes` | number | Currently used GPU memory |
| `free_bytes` | number | Free GPU memory |
| `reserved_bytes` | number | Reserved GPU memory |
| `used_percent` | number | Memory usage percentage (0-100) |
| `allocated_blocks` | number | Number of allocated memory blocks |
| `utilized_blocks` | number | Number of utilized memory blocks |
| `free_blocks` | number | Number of free memory blocks |
| `atomic_allocations_bytes` | number | Total atomic memory allocations |
| `fragmentation_ratio` | number | Memory fragmentation (0-1) |
| `processes` | array | GPU processes with memory usage |
| `threads` | array | Thread information |
| `blocks` | array | Memory block details |
| `nsight_metrics` | object | Nsight Compute metrics per PID |

#### `GET /vram/stream`

Returns Server-Sent Events (SSE) stream with real-time VRAM metrics.

**Response:** Continuous stream of SSE events
```
data: {"total_bytes":...,"used_bytes":...}

data: {"total_bytes":...,"used_bytes":...}
...
```

**Update Interval:** ~500ms

**Usage Example:**
```bash
curl -N http://localhost:6767/vram/stream
```

### Error Responses

**404 Not Found**
```
Not Found
```
Returned for unknown endpoints.

**500 Internal Server Error**
Returned on server errors (check server logs).

---

## Integration Guide

### Python Client Example

```python
import requests
import json

def get_vram_metrics(url="http://localhost:6767"):
    """Fetch VRAM metrics from Blackbox server."""
    response = requests.get(f"{url}/vram")
    response.raise_for_status()
    return response.json()

# Usage
metrics = get_vram_metrics()
print(f"GPU Memory Usage: {metrics['used_percent']:.2f}%")
print(f"Total: {metrics['total_bytes'] / 1024**3:.2f} GB")
print(f"Used: {metrics['used_bytes'] / 1024**3:.2f} GB")
```

### Streaming Client Example

```python
import requests
import json

def stream_vram_metrics(url="http://localhost:6767"):
    """Stream VRAM metrics from Blackbox server."""
    response = requests.get(f"{url}/vram/stream", stream=True)
    
    for line in response.iter_lines():
        if line:
            line_str = line.decode('utf-8')
            if line_str.startswith('data: '):
                data_str = line_str[6:]  # Remove 'data: ' prefix
                try:
                    metrics = json.loads(data_str)
                    yield metrics
                except json.JSONDecodeError:
                    continue

# Usage
for metrics in stream_vram_metrics():
    print(f"Memory: {metrics['used_percent']:.2f}%")
```

### cURL Examples

```bash
# Single request
curl http://localhost:6767/vram | jq

# Streaming
curl -N http://localhost:6767/vram/stream

# Custom port
curl http://localhost:9000/vram
```

### JavaScript/Node.js Example

```javascript
// Single request
async function getVRAMMetrics(url = 'http://localhost:6767') {
    const response = await fetch(`${url}/vram`);
    return await response.json();
}

// Streaming
async function streamVRAMMetrics(url = 'http://localhost:6767') {
    const response = await fetch(`${url}/vram/stream`);
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    
    while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        
        const chunk = decoder.decode(value);
        const lines = chunk.split('\n');
        
        for (const line of lines) {
            if (line.startsWith('data: ')) {
                const data = JSON.parse(line.slice(6));
                console.log(data);
            }
        }
    }
}
```

---

## Implementation Details

### Build System

The server uses CMake with automatic dependency management:

- **Boost**: Downloaded and built automatically (system library only)
- **Abseil**: Fetched via Git and built
- **NVML**: Optional, detected at compile time

### Compilation

```bash
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

### Port Configuration

Default port: **6767**

Override with command-line argument:
```bash
./build/blackbox-server 8080
```

### NVML Integration

NVML is optional. If not found:
- Server compiles without NVML features
- `nsight_metrics` will be empty
- Basic memory info may be unavailable

To enable NVML:
```bash
sudo apt install -y libnvidia-ml-dev
```

### Nsight Compute Integration

Nsight Compute (NCU) provides detailed GPU profiling:

- **Automatic Detection**: Server checks for `ncu` command
- **Per-Process Profiling**: Metrics collected per PID
- **Non-blocking**: Profiling failures don't crash server
- **Timeout**: 2-second timeout per profile

To install Nsight Compute:
```bash
# Download from NVIDIA Developer site
# Extract to ~/nsight-compute
# Add to PATH or create symlink
sudo ln -s ~/nsight-compute/ncu /usr/local/bin/ncu
```

### Memory Block Tracking

Memory blocks are tracked at the system level:
- **Allocated**: Memory blocks reserved by processes
- **Utilized**: Blocks actively in use (from Nsight Compute if available)
- **Type**: Classification (kv_cache, activation, weight, other)

### Error Handling

- **Connection Errors**: Gracefully handled, server continues
- **NVML Errors**: Logged, server continues without NVML features
- **Nsight Errors**: Silent failure, metrics marked as unavailable

---

## Troubleshooting

### Server Won't Start

**Issue**: Port already in use
```bash
# Check what's using the port
sudo lsof -i :6767

# Kill the process or use a different port
./build/blackbox-server 8080
```

**Issue**: Permission denied
```bash
# Bind to port < 1024 requires root
sudo ./build/blackbox-server 80
```

### No GPU Metrics

**Issue**: NVML not found
```bash
# Check if NVML is installed
find /usr -name nvml.h

# Install NVML
sudo apt install -y libnvidia-ml-dev
```

**Issue**: No GPU detected
```bash
# Check GPU
nvidia-smi

# If not found, install drivers
sudo apt install -y nvidia-driver-535
sudo reboot
```

### Nsight Compute Not Working

**Issue**: `ncu` command not found
```bash
# Check if ncu is in PATH
which ncu

# Install or add to PATH
export PATH=$PATH:~/nsight-compute
```

**Issue**: Profiling timeout
- Nsight Compute requires active CUDA kernels
- Metrics only available when processes are running GPU code
- Timeout is intentional (2 seconds) to avoid blocking

### Build Errors

**Issue**: CMake version too old
```bash
# Upgrade CMake
sudo apt install -y cmake
# Or build from source
```

**Issue**: Compiler not C++17 compatible
```bash
# Check compiler version
g++ --version  # Need 7+
clang++ --version  # Need 5+

# Install newer compiler
sudo apt install -y g++-9
```

### Performance Issues

**Issue**: High CPU usage
- Nsight Compute profiling is CPU-intensive
- Reduce profiling frequency or disable if not needed
- Consider using `/vram` instead of `/vram/stream` for polling

**Issue**: Slow responses
- Check network latency
- Verify NVML is working (faster than Nsight Compute)
- Disable Nsight Compute if not needed

---

## Additional Resources

- [NVML Documentation](https://docs.nvidia.com/deploy/nvml-api/nvml-api-reference.html)
- [Nsight Compute Documentation](https://docs.nvidia.com/nsight-compute/)
- [Boost.Beast Documentation](https://www.boost.org/doc/libs/1_84_0/libs/beast/doc/html/index.html)
- [CMake Documentation](https://cmake.org/documentation/)

---

## License

See main repository LICENSE file.

