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
  "total_bytes": 34359738368,
  "used_bytes": 8589934592,
  "free_bytes": 25769803776,
  "reserved_bytes": 8589934592,
  "used_percent": 25.00,
  "active_blocks": 1,
  "free_blocks": 0,
  "atomic_allocations_bytes": 8589934592,
  "fragmentation_ratio": 0.25,
  "processes": [...],
  "threads": [...],
  "blocks": [...],
  "nsight_metrics": {...}
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
| `active_blocks` | integer | Number of active memory blocks |
| `free_blocks` | integer | Number of free memory blocks |
| `atomic_allocations_bytes` | integer | Total atomic memory allocations |
| `fragmentation_ratio` | float | Memory fragmentation ratio (0-1) |
| `processes` | array | GPU processes array |
| `threads` | array | Thread information array |
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
| `block_id` | integer | Block identifier |
| `address` | integer | Memory address (0 if unknown) |
| `size` | integer | Block size in bytes |
| `type` | string | Block type ("kv_cache", "activation", "weight", "other") |
| `allocated` | boolean | Whether block is allocated |
| `utilized` | boolean | Whether block is actively used |

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
| `atomic_operations` | integer | Count of atomic operations |
| `threads_per_block` | integer | CUDA threads per block |
| `occupancy` | float | GPU occupancy percentage |
| `active_blocks` | integer | Active CUDA blocks |
| `memory_throughput` | integer | Memory throughput (bytes/sec) |
| `dram_read_bytes` | integer | DRAM read bytes |
| `dram_write_bytes` | integer | DRAM write bytes |
| `available` | boolean | Whether metrics are available |

**Example:**
```bash
curl http://localhost:6767/vram | jq
```

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

