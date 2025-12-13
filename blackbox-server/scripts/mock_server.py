from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import time
import random
from datetime import datetime

# Mock Prometheus metrics string (vLLM metrics)
MOCK_VLLM_METRICS = """# HELP python_gc_objects_collected_total Objects collected during gc
# TYPE python_gc_objects_collected_total counter
python_gc_objects_collected_total{generation="0"} 12662.0
python_gc_objects_collected_total{generation="1"} 1252.0
python_gc_objects_collected_total{generation="2"} 1352.0
# HELP vllm:num_requests_running Number of requests in model execution batches.
# TYPE vllm:num_requests_running gauge
vllm:num_requests_running{engine="0",model_name="Qwen/Qwen2.5-7B-Instruct"} 0.0
# HELP vllm:num_requests_waiting Number of requests waiting to be processed.
# TYPE vllm:num_requests_waiting gauge
vllm:num_requests_waiting{engine="0",model_name="Qwen/Qwen2.5-7B-Instruct"} 0.0
# HELP vllm:kv_cache_usage_perc KV-cache usage. 1 means 100 percent usage.
# TYPE vllm:kv_cache_usage_perc gauge
vllm:kv_cache_usage_perc{engine="0",model_name="Qwen/Qwen2.5-7B-Instruct"} 0.0"""

class MockVRAMHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/vram':
            total_bytes = 42949672960
            used_bytes = random.randint(30000000000, 40000000000)
            free_bytes = total_bytes - used_bytes
            reserved_bytes = used_bytes
            used_percent = (used_bytes / total_bytes) * 100.0
            active_blocks = random.randint(1, 5)
            free_blocks = random.randint(0, 2)
            atomic_allocations_bytes = used_bytes
            fragmentation_ratio = used_percent / 100.0
            
            # Generate mock processes
            processes = []
            if random.random() > 0.3:  # 70% chance of having a process
                process_used = random.randint(25000000000, 35000000000)
                processes.append({
                    "pid": random.randint(100000, 200000),
                    "name": "VLLM::EngineCor",
                    "used_bytes": process_used,
                    "reserved_bytes": process_used
                })
            
            # Generate mock threads
            threads = []
            for i, proc in enumerate(processes):
                threads.append({
                    "thread_id": i,
                    "allocated_bytes": proc["used_bytes"],
                    "state": "active"
                })
            
            vram_data = {
                "total_bytes": total_bytes,
                "used_bytes": used_bytes,
                "free_bytes": free_bytes,
                "reserved_bytes": reserved_bytes,
                "used_percent": round(used_percent, 2),
                "active_blocks": active_blocks,
                "free_blocks": free_blocks,
                "atomic_allocations_bytes": atomic_allocations_bytes,
                "fragmentation_ratio": round(fragmentation_ratio, 4),
                "processes": processes,
                "threads": threads,
                "blocks": [],
                "vllm_metrics": MOCK_VLLM_METRICS
            }
            
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(json.dumps(vram_data, indent=2).encode())
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b'Not Found')
    
    def log_message(self, format, *args):
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {args[0]}")

def run(port=8080):
    server_address = ('', port)
    httpd = HTTPServer(server_address, MockVRAMHandler)
    print(f"Mock VRAM server running on http://127.0.0.1:{port}")
    print(f"VRAM endpoint: http://127.0.0.1:{port}/vram")
    print("Press Ctrl+C to stop")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("Shutting down server...")
        httpd.shutdown()

if __name__ == '__main__':
    run()

