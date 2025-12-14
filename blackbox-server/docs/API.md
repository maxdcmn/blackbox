# API Reference

Complete API documentation for Blackbox Server.

## Base URL

```
http://localhost:6767
```

Default port: **6767** (configurable via command-line argument)

## Endpoints

### GET /vram

Returns current VRAM metrics as JSON.

**Request:**
```http
GET /vram HTTP/1.1
Host: localhost:6767
```

**Response:**
```http
HTTP/1.1 200 OK
Content-Type: application/json

{
  "total_bytes": 42949672960,
  "used_bytes": 34561064960,
  "free_bytes": 8388608000,
  "reserved_bytes": 34561064960,
  "used_percent": 80.45,
  "allocated_blocks": 14401,
  "utilized_blocks": 0,
  "free_blocks": 14401,
  "atomic_allocations_bytes": 31299958784,
  "fragmentation_ratio": 0.8045,
  "processes": [
    {
      "pid": 131963,
      "name": "VLLM::EngineCor",
      "used_bytes": 31299958784,
      "reserved_bytes": 31299958784
    }
  ],
  "threads": [],
  "blocks": [
    {
      "block_id": 0,
      "address": 0,
      "size": 2173952,
      "type": "kv_cache",
      "allocated": true,
      "utilized": false
    }
  ],
  "nsight_metrics": {
    "131963": {
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

#### Top-Level Fields

| Field | Type | Description |
|-------|------|-------------|
| `total_bytes` | integer | Total GPU memory in bytes |
| `used_bytes` | integer | Currently used GPU memory |
| `free_bytes` | integer | Free GPU memory |
| `reserved_bytes` | integer | Reserved GPU memory |
| `used_percent` | float | Memory usage percentage (0-100) |
| `allocated_blocks` | integer | Total allocated KV cache blocks (from vLLM) |
| `utilized_blocks` | integer | Blocks actively storing data (calculated from vLLM's `kv_cache_usage_perc`) |
| `free_blocks` | integer | Allocated but unused blocks (calculated: `allocated_blocks - utilized_blocks`) |
| `atomic_allocations_bytes` | integer | Total atomic memory allocations |
| `fragmentation_ratio` | float | Memory fragmentation ratio (0-1) |
| `processes` | array | GPU processes array |
| `threads` | array | Empty array (removed - was redundant mapping of processes) |
| `blocks` | array | Memory block details array |
| `nsight_metrics` | object | Nsight Compute metrics per PID |

#### Process Object

```json
{
  "pid": 12345,
  "name": "python",
  "used_bytes": 8589934592,
  "reserved_bytes": 8589934592
}
```

| Field | Type | Description |
|-------|------|-------------|
| `pid` | integer | Process ID |
| `name` | string | Process name |
| `used_bytes` | integer | Memory used by process |
| `reserved_bytes` | integer | Memory reserved by process |

#### Thread Object

```json
{
  "thread_id": 0,
  "allocated_bytes": 8589934592,
  "state": "active"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `thread_id` | integer | Thread identifier |
| `allocated_bytes` | integer | Memory allocated to thread |
| `state` | string | Thread state ("active", "running", "waiting") |

#### Block Object

```json
{
  "block_id": 0,
  "address": 0,
  "size": 16384,
  "type": "other",
  "allocated": true,
  "utilized": true
}
```

| Field | Type | Description |
|-------|------|-------------|
| `block_id` | integer | Block identifier (0 to `allocated_blocks-1`) |
| `address` | integer | Memory address (0 if unknown, vLLM doesn't expose addresses) |
| `size` | integer | Block size in bytes (calculated from NVML process memory / num_blocks) |
| `type` | string | Block type ("kv_cache" for vLLM blocks) |
| `allocated` | boolean | Whether block is allocated (always `true` for vLLM blocks) |
| `utilized` | boolean | Whether block is actively storing data (from vLLM's `kv_cache_usage_perc`) |

#### Nsight Metrics Object

```json
{
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
```

Key: Process ID (string)

| Field | Type | Description |
|-------|------|-------------|
| `atomic_operations` | integer | Count of atomic operations (from Nsight Compute) |
| `threads_per_block` | integer | CUDA threads per block (from Nsight Compute) |
| `occupancy` | float | GPU occupancy percentage (from Nsight Compute) |
| `active_blocks` | integer | Active CUDA blocks (not parsed, always 0) |
| `memory_throughput` | integer | Memory throughput (not parsed, always 0) |
| `dram_read_bytes` | integer | DRAM read bytes (from Nsight Compute) |
| `dram_write_bytes` | integer | DRAM write bytes (from Nsight Compute) |
| `available` | boolean | Whether Nsight Compute metrics are available |

**Example:**
```bash
curl http://localhost:6767/vram | jq
```

**Data Sources:**

- **NVML (NVIDIA Management Library)**: System-level GPU memory (`total_bytes`, `used_bytes`, `free_bytes`), process-level memory usage (`processes[]`)
- **vLLM Metrics API**: Block allocation data (`allocated_blocks` from `vllm:cache_config_info`), KV cache utilization (`utilized` from `vllm:kv_cache_usage_perc`)
- **Nsight Compute (NCU)**: GPU activity metrics (`atomic_operations`, `threads_per_block`, `occupancy`, `dram_read_bytes`, `dram_write_bytes`)
- **Calculated Fields**: `free_blocks` (allocated_blocks - utilized), `fragmentation_ratio` (1 - free/total), `block.size` (process_memory / num_blocks)

**Key Metrics:**

- **`allocated_blocks`**: Total blocks vLLM has allocated for KV cache (from vLLM)
- **`utilized_blocks`**: Count of blocks actively storing data (calculated from vLLM's `kv_cache_usage_perc`)
- **`utilized`** (per block): Whether block is actively storing data (boolean in `blocks[]` array)
- **`free_blocks`**: Allocated but unused blocks = `allocated_blocks - utilized_blocks`

---

### GET /vram/stream

Returns Server-Sent Events (SSE) stream with real-time VRAM metrics.

**Request:**
```http
GET /vram/stream HTTP/1.1
Host: localhost:6767
```

**Response:**
```http
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive

data: {"total_bytes":34359738368,"used_bytes":8589934592,...}

data: {"total_bytes":34359738368,"used_bytes":8599934592,...}

...
```

**Update Interval:** ~500ms

**Event Format:**
- Each event is a JSON object
- Prefixed with `data: `
- Followed by two newlines (`\n\n`)

**Example:**
```bash
curl -N http://localhost:6767/vram/stream
```

**Python Example:**
```python
import requests
import json

response = requests.get('http://localhost:6767/vram/stream', stream=True)
for line in response.iter_lines():
    if line and line.startswith(b'data: '):
        data = json.loads(line[6:])
        print(f"Memory: {data['used_percent']:.2f}%")
```

---

## Error Responses

### 404 Not Found

Returned for unknown endpoints.

**Response:**
```http
HTTP/1.1 404 Not Found
Content-Type: text/plain

Not Found
```

### 500 Internal Server Error

Returned on server errors. Check server logs for details.

**Response:**
```http
HTTP/1.1 500 Internal Server Error
Content-Type: text/plain

Internal Server Error
```

---

## Rate Limiting

No rate limiting is currently implemented. However:
- `/vram/stream` maintains persistent connections
- Each connection consumes server resources
- Consider connection pooling for multiple clients

---

## CORS

CORS headers are not set by default. For web browser access, you may need to:
1. Use a reverse proxy (nginx, Caddy) with CORS headers
2. Modify the server code to add CORS headers
3. Use server-side proxy for API calls

---

## Authentication

No authentication is currently implemented. For production:
1. Use firewall rules to restrict access
2. Implement API key authentication
3. Use reverse proxy with authentication (nginx, Traefik)
4. Run behind VPN or private network

---

## Best Practices

### Polling vs Streaming

- **Polling (`/vram`)**: Use for periodic checks, dashboards, monitoring systems
- **Streaming (`/vram/stream`)**: Use for real-time displays, live monitoring

### Error Handling

Always handle network errors and timeouts:

```python
import requests
from requests.exceptions import RequestException, Timeout

try:
    response = requests.get('http://localhost:6767/vram', timeout=5)
    response.raise_for_status()
    data = response.json()
except Timeout:
    print("Request timed out")
except RequestException as e:
    print(f"Request failed: {e}")
```

### Connection Management

For streaming, handle reconnection:

```python
import time
import requests

def stream_with_reconnect(url, max_retries=5):
    for attempt in range(max_retries):
        try:
            response = requests.get(url, stream=True, timeout=None)
            for line in response.iter_lines():
                if line:
                    yield line
        except Exception as e:
            print(f"Connection lost: {e}")
            if attempt < max_retries - 1:
                time.sleep(2 ** attempt)  # Exponential backoff
            else:
                raise
```

---

## Examples

See [README.md](README.md#integration-guide) for complete integration examples.

