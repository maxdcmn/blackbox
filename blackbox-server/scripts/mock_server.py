from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import time
import random
from datetime import datetime

class MockSnapshotHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/v1/snapshot':
            snapshot = {
                "version": "mock-vllm-1.0.0",
                "ts": int(time.time() * 1000),
                "requests": {
                    "queue_len": random.randint(0, 10),
                    "p50_ms": random.randint(50, 200),
                    "p95_ms": random.randint(200, 500)
                },
                "tokens": {
                    "tps": round(random.uniform(10.0, 100.0), 1),
                    "total": random.randint(1000, 100000),
                    "by_intent": {
                        "question": random.randint(1000, 10000),
                        "answer": random.randint(5000, 50000),
                        "summary": random.randint(500, 5000)
                    }
                },
                "kv": {
                    "used_mb": random.randint(1000, 8000),
                    "capacity_mb": 10000,
                    "compressed_mb": random.randint(500, 2000)
                }
            }
            
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(json.dumps(snapshot, indent=2).encode())
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b'Not Found')
    
    def log_message(self, format, *args):
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {args[0]}")

def run(port=8001):
    server_address = ('', port)
    httpd = HTTPServer(server_address, MockSnapshotHandler)
    print(f"Mock vLLM server running on http://127.0.0.1:{port}")
    print(f"Snapshot endpoint: http://127.0.0.1:{port}/v1/snapshot")
    print("Press Ctrl+C to stop")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("Shutting down server...")
        httpd.shutdown()

if __name__ == '__main__':
    run()

