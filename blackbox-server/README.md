# Blackbox Server

High-performance GPU VRAM monitoring server with NVML and Nsight Compute integration.

## Quick Start

```bash
# Build (from project root)
make

# Run
make run
# Or manually: ./blackbox-server/build/blackbox-server 6767

# Test
curl http://localhost:6767/vram
```

## Documentation

- **[Setup Guide](docs/SETUP.md)** - Installation instructions
- **[API Reference](docs/API.md)** - API endpoints and examples
- **[Implementation Details](docs/IMPLEMENTATION.md)** - Internal architecture

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

