from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import random
from datetime import datetime

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
            used_bytes = random.randint(int(total_bytes * 0.2), int(total_bytes * 1.0))
            free_bytes = total_bytes - used_bytes
            reserved_bytes = int(used_bytes * random.uniform(0.95, 1.05))
            used_percent = (used_bytes / total_bytes) * 100.0
            
            total_blocks = 14000
            active_blocks = random.randint(int(total_blocks * 0.2), total_blocks)
            free_blocks_count = total_blocks - active_blocks
            allocated_blocks = total_blocks
            
            block_size = random.choice([8 * 1024, 16 * 1024, 32 * 1024, 64 * 1024])
            blocks = []
            allocated_list = random.sample(range(total_blocks), allocated_blocks)
            active_list = random.sample(allocated_list, min(active_blocks, len(allocated_list)))
            
            for i in range(min(total_blocks, 2000)):
                is_allocated = i in allocated_list if i < len(allocated_list) else False
                is_utilized = i in active_list if i < len(active_list) else False
                blocks.append({
                    "block_id": i,
                    "address": i * block_size,
                    "size": block_size,
                    "type": "kv_cache",
                    "allocated": is_allocated,
                    "utilized": is_utilized
                })
            
            fragmentation_ratio = random.uniform(0.7, 1.0)
            atomic_allocations_bytes = int(used_bytes * random.uniform(0.2, 1.0))
            
            num_processes = random.randint(20, 40)
            processes = []
            remaining_bytes = used_bytes
            process_names = ["VLLM::EngineCor", "python", "vllm-worker", "torch-compile", "vllm::LLMEngine", "cuda-serv", "model-loader", "tokenizer", "inference-engine", "gpu-monitor", "cache-manager"]
            
            for i in range(num_processes):
                if i == num_processes - 1:
                    process_used = max(0, remaining_bytes)
                else:
                    max_per_process = remaining_bytes // (num_processes - i)
                    process_used = random.randint(int(max_per_process * 0.3), int(max_per_process * 0.9))
                process_used = max(0, min(process_used, remaining_bytes))
                remaining_bytes -= process_used
                
                processes.append({
                    "pid": random.randint(10000, 999999),
                    "name": random.choice(process_names),
                    "used_bytes": process_used,
                    "reserved_bytes": int(process_used * random.uniform(0.90, 1.10))
                })
            
            threads = []
            thread_id_counter = 0
            for proc in processes:
                num_threads = random.randint(20, 40)
                thread_bytes_total = proc["used_bytes"]
                
                for j in range(num_threads):
                    if j == num_threads - 1:
                        thread_allocated = thread_bytes_total
                    else:
                        thread_allocated = random.randint(int(thread_bytes_total * 0.05), int(thread_bytes_total * 0.4))
                        thread_bytes_total -= thread_allocated
                    
                    threads.append({
                        "thread_id": thread_id_counter,
                        "allocated_bytes": max(0, thread_allocated),
                        "state": random.choice(["active", "idle", "waiting", "running", "blocked", "sleeping"])
                    })
                    thread_id_counter += 1
            
            vram_data = {
                "total_bytes": total_bytes,
                "used_bytes": used_bytes,
                "free_bytes": free_bytes,
                "reserved_bytes": reserved_bytes,
                "used_percent": round(used_percent, 2),
                "total_blocks": total_blocks,
                "allocated_blocks": allocated_blocks,
                "active_blocks": active_blocks,
                "free_blocks": free_blocks_count,
                "atomic_allocations_bytes": atomic_allocations_bytes,
                "fragmentation_ratio": round(fragmentation_ratio, 4),
                "processes": processes,
                "threads": threads,
                "blocks": blocks[:1000],
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
