# Blackbox Server

High-performance GPU VRAM monitoring server with NVML and Nsight Compute integration.

## Quick Start

```bash
# Install dependencies
./scripts/install_deps.sh

# Build
./scripts/setup.sh

# Run
./build/blackbox-server

# Test
curl http://localhost:6767/vram
```

## Documentation

- **[Complete Documentation](docs/README.md)** - Full documentation
- **[Setup Guide](docs/SETUP.md)** - Installation instructions
- **[API Reference](docs/API.md)** - API endpoints and examples

## Features

- Real-time GPU VRAM monitoring
- Process-level memory tracking
- Nsight Compute integration for detailed GPU metrics
- Server-Sent Events (SSE) streaming support
- Lightweight and fast

## Requirements

- NVIDIA GPU with drivers
- Linux (Ubuntu/Debian)
- CMake 3.15+
- C++17 compiler

## API Endpoints

- `GET /vram` - JSON response with current metrics
- `GET /vram/stream` - SSE stream with real-time updates

See [API Reference](docs/API.md) for details.

