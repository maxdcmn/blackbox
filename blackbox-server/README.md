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
- Smart HuggingFace model deployment with concurrent limits
- Model lifecycle management (deploy/spindown)
- Lightweight and fast

## Requirements

- NVIDIA GPU with drivers
- Linux (Ubuntu/Debian)
- CMake 3.15+
- C++17 compiler

## Configuration

Copy `.env.example` to `.env` and configure your environment variables:

```bash
cp .env.example .env
```

**Required (for model deployment):**
- `HF_TOKEN` - HuggingFace API token

**Optional:**
- `MAX_CONCURRENT_MODELS` - Maximum concurrent models (default: 3)
- `GPU_TYPE` - GPU type override (T4, A100, H100, L40) or leave empty for auto-detection

## API Endpoints

- `GET /vram` - JSON response with current metrics
- `GET /vram/stream` - SSE stream with real-time updates
- `POST /deploy` - Deploy HuggingFace models using vLLM Docker (with smart limits)
- `POST /spindown` - Stop and remove deployed models
- `GET /models` - List all deployed models and their status
- `POST /optimize` - Optimize GPU utilization by restarting overallocated models

See [API Reference](docs/API.md) for details.

### Deploy a Model

```bash
curl -X POST http://localhost:6767/deploy \
  -H "Content-Type: application/json" \
  -d '{
    "model_id": "Qwen/Qwen2.5-7B-Instruct",
    "hf_token": "hf_xxxxxxxxxxxxx",
    "port": 8000
  }'
```

**Note:** Deployment is limited by `MAX_CONCURRENT_MODELS` in `.env` (default: 3). Use `GET /models` to check current deployments.

### Spindown a Model

```bash
curl -X POST http://localhost:6767/spindown \
  -H "Content-Type: application/json" \
  -d '{"model_id": "Qwen/Qwen2.5-7B-Instruct"}'
```

### List Deployed Models

```bash
curl http://localhost:6767/models | jq
```

