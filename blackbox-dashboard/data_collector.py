#!/usr/bin/env python3
"""
Data Collector - Fetches VRAM data and submits to API
Bridges the gap between the VRAM source and the dashboard database
"""
import requests
import time
import sys
import argparse
import threading
from datetime import datetime
from typing import Optional, Dict, Any, List


class NodeCollector:
    """Collector for a single node"""
    def __init__(self, node_id: int, node_name: str, vram_url: str,
                 api_url: str, interval: int = 5):
        self.node_id = node_id
        self.node_name = node_name
        self.vram_url = vram_url
        self.api_url = api_url
        self.interval = interval
        self.running = False
        self.thread = None
        self.snapshot_count = 0
        self.error_count = 0

    def fetch_vram_data(self) -> Optional[Dict[str, Any]]:
        """Fetch data from VRAM endpoint"""
        try:
            response = requests.get(self.vram_url, timeout=10)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"[{self.node_name}] Error fetching VRAM data: {e}", file=sys.stderr)
            return None

    def submit_to_api(self, data: Dict[str, Any]) -> bool:
        """Submit data to the dashboard API"""
        try:
            # Transform data to match API schema
            payload = {
                "node_id": self.node_id,  # Include node_id
                "timestamp": datetime.utcnow().isoformat(),
                "total_bytes": data.get("total_bytes", 0),
                "used_bytes": data.get("used_bytes", 0),
                "free_bytes": data.get("free_bytes", 0),
                "reserved_bytes": data.get("reserved_bytes", 0),
                "used_percent": data.get("used_percent", 0.0),
                "active_blocks": data.get("active_blocks", 0),
                "free_blocks": data.get("free_blocks", 0),
                "atomic_allocations_bytes": data.get("atomic_allocations_bytes", 0),
                "fragmentation_ratio": data.get("fragmentation_ratio", 0.0),
                "processes": [
                    {
                        "pid": p.get("pid"),
                        "name": p.get("name", "unknown"),
                        "used_bytes": p.get("used_bytes", 0),
                        "reserved_bytes": p.get("reserved_bytes", 0)
                    }
                    for p in data.get("processes", [])
                ],
                "threads": [
                    {
                        "thread_id": t.get("thread_id"),
                        "allocated_bytes": t.get("allocated_bytes", 0),
                        "state": t.get("state", "unknown")
                    }
                    for t in data.get("threads", [])
                ],
                "blocks": [
                    {
                        "block_id": b.get("block_id"),
                        "size": b.get("size", 0),
                        "type": b.get("type", "unknown"),
                        "allocated": b.get("allocated", False),
                        "utilized": b.get("utilized", False)
                    }
                    for b in data.get("blocks", [])
                ],
                "nsight_metrics": data.get("nsight_metrics", {}),
                "vllm_metrics": data.get("vllm_metrics", "")
            }

            response = requests.post(
                f"{self.api_url}/snapshots",
                json=payload,
                timeout=10
            )
            response.raise_for_status()
            return True

        except requests.exceptions.RequestException as e:
            print(f"[{self.node_name}] Error submitting to API: {e}", file=sys.stderr)
            return False

    def collect_loop(self):
        """Main collection loop (runs in thread)"""
        print(f"[{self.node_name}] Starting collector (interval: {self.interval}s)")

        while self.running:
            # Fetch data
            data = self.fetch_vram_data()

            if data:
                # Submit to API
                success = self.submit_to_api(data)

                if success:
                    self.snapshot_count += 1
                    print(f"[{datetime.now().strftime('%H:%M:%S')}] [{self.node_name}] "
                          f"Snapshot #{self.snapshot_count} "
                          f"(Used: {data.get('used_bytes', 0) / (1024**3):.2f} GB, "
                          f"{data.get('used_percent', 0):.1f}%)")
                    self.error_count = 0  # Reset error count on success
                else:
                    self.error_count += 1
            else:
                self.error_count += 1

            # If too many errors, wait longer before retry
            sleep_time = self.interval
            if self.error_count > 5:
                sleep_time = min(60, self.interval * 2)

            time.sleep(sleep_time)

        print(f"[{self.node_name}] Stopped (collected {self.snapshot_count} snapshots)")

    def start(self):
        """Start collection thread"""
        self.running = True
        self.thread = threading.Thread(target=self.collect_loop, daemon=True)
        self.thread.start()

    def stop(self):
        """Stop collection thread"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)


class MultiNodeCollector:
    """Manages collectors for multiple nodes"""
    def __init__(self, api_url: str = "http://localhost:8001/api", interval: int = 5):
        self.api_url = api_url
        self.interval = interval
        self.collectors = {}

    def fetch_nodes(self) -> List[Dict[str, Any]]:
        """Fetch list of enabled nodes from API"""
        try:
            response = requests.get(f"{self.api_url}/nodes", timeout=10)
            response.raise_for_status()
            nodes = response.json()
            return [n for n in nodes if n.get('enabled', True)]
        except requests.exceptions.RequestException as e:
            print(f"Error fetching nodes from API: {e}", file=sys.stderr)
            return []

    def sync_collectors(self):
        """Sync collectors with enabled nodes"""
        nodes = self.fetch_nodes()

        # Get current node IDs
        current_ids = {n['id'] for n in nodes}
        existing_ids = set(self.collectors.keys())

        # Stop collectors for removed/disabled nodes
        for node_id in existing_ids - current_ids:
            print(f"Stopping collector for node ID {node_id}")
            self.collectors[node_id].stop()
            del self.collectors[node_id]

        # Start collectors for new nodes
        for node in nodes:
            node_id = node['id']
            if node_id not in existing_ids:
                vram_url = f"http://{node['host']}:{node['port']}/vram"
                collector = NodeCollector(
                    node_id=node_id,
                    node_name=node['name'],
                    vram_url=vram_url,
                    api_url=self.api_url,
                    interval=self.interval
                )
                collector.start()
                self.collectors[node_id] = collector
                print(f"Started collector for '{node['name']}' ({vram_url})")

    def run(self):
        """Main loop - periodically sync with API"""
        print("Multi-Node VRAM Data Collector")
        print(f"API endpoint: {self.api_url}")
        print(f"Polling interval: {self.interval}s")
        print()

        try:
            while True:
                # Sync collectors with API
                self.sync_collectors()

                if not self.collectors:
                    print("No enabled nodes found. Waiting...")

                # Wait before next sync
                time.sleep(30)  # Sync every 30 seconds

        except KeyboardInterrupt:
            print("\n\nStopping all collectors...")
            for collector in self.collectors.values():
                collector.stop()
            print("Done!")


def main():
    parser = argparse.ArgumentParser(
        description='Collect VRAM data and submit to dashboard API (Multi-Node)'
    )
    parser.add_argument(
        '--api-url',
        default='http://localhost:8001/api',
        help='Dashboard API URL (default: http://localhost:8001/api)'
    )
    parser.add_argument(
        '--interval',
        type=int,
        default=5,
        help='Collection interval in seconds (default: 5)'
    )
    parser.add_argument(
        '--single-node',
        action='store_true',
        help='Legacy single-node mode (requires --node-id, --host, --port)'
    )
    parser.add_argument(
        '--node-id',
        type=int,
        help='Node ID for single-node mode'
    )
    parser.add_argument(
        '--host',
        help='VRAM server host for single-node mode'
    )
    parser.add_argument(
        '--port',
        type=int,
        default=6767,
        help='VRAM server port for single-node mode (default: 6767)'
    )

    args = parser.parse_args()

    if args.single_node:
        # Legacy single-node mode
        if not args.node_id or not args.host:
            print("Error: --single-node requires --node-id and --host", file=sys.stderr)
            sys.exit(1)

        vram_url = f"http://{args.host}:{args.port}/vram"
        collector = NodeCollector(
            node_id=args.node_id,
            node_name=f"node-{args.node_id}",
            vram_url=vram_url,
            api_url=args.api_url,
            interval=args.interval
        )
        collector.running = True
        try:
            collector.collect_loop()
        except KeyboardInterrupt:
            print("\nStopping...")
    else:
        # Multi-node mode (default)
        collector = MultiNodeCollector(
            api_url=args.api_url,
            interval=args.interval
        )
        collector.run()


if __name__ == '__main__':
    main()