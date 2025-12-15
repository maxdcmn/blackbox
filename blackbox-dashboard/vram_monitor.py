#!/usr/bin/env python3
"""
VRAM Monitor - Timeseries tracking for GPU memory usage
Polls VRAM API every 5 seconds and tracks metrics history
"""
import json
import sys
import time
import argparse
import requests
from datetime import datetime
from pathlib import Path
from typing import Dict, Any, List, Set, Optional
from collections import defaultdict


class VRAMMonitor:
    def __init__(self, url: str = "http://localhost:8080/vram",
                 interval: int = 5,
                 history_file: Optional[str] = None):
        self.url = url
        self.interval = interval
        self.history_file = history_file or str(Path.home() / ".blackbox_vram_history.json")

        # Track seen IDs to detect new requests
        self.seen_process_pids: Set[int] = set()
        self.seen_block_ids: Set[int] = set()
        self.seen_thread_ids: Set[int] = set()

        # Timeseries data
        self.history: List[Dict[str, Any]] = []
        self.max_history_size = 1000  # Keep last 1000 snapshots

        # Statistics
        self.new_processes: List[Dict[str, Any]] = []
        self.new_blocks: List[Dict[str, Any]] = []
        self.new_threads: List[Dict[str, Any]] = []

        # Load existing history if available
        self.load_history()

    def load_history(self):
        """Load existing history from file"""
        try:
            if Path(self.history_file).exists():
                with open(self.history_file, 'r') as f:
                    data = json.load(f)
                    self.history = data.get('history', [])
                    # Restore seen IDs from last snapshot
                    if self.history:
                        last = self.history[-1]
                        self.seen_process_pids = set(p['pid'] for p in last.get('processes', []))
                        self.seen_block_ids = set(b.get('block_id', -1) for b in last.get('blocks', []) if b.get('block_id') is not None)
                        self.seen_thread_ids = set(t.get('thread_id', -1) for t in last.get('threads', []) if t.get('thread_id') is not None)
                print(f"Loaded {len(self.history)} historical snapshots from {self.history_file}")
        except Exception as e:
            print(f"Could not load history: {e}", file=sys.stderr)

    def save_history(self):
        """Save history to file"""
        try:
            # Keep only last N snapshots
            if len(self.history) > self.max_history_size:
                self.history = self.history[-self.max_history_size:]

            data = {
                'history': self.history,
                'last_updated': datetime.now().isoformat(),
                'total_snapshots': len(self.history)
            }

            with open(self.history_file, 'w') as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            print(f"Could not save history: {e}", file=sys.stderr)

    def fetch_vram(self) -> Optional[Dict[str, Any]]:
        """Fetch VRAM data from API"""
        try:
            response = requests.get(self.url, timeout=10)
            response.raise_for_status()
            return response.json()
        except requests.exceptions.RequestException as e:
            print(f"Error fetching from {self.url}: {e}", file=sys.stderr)
            return None

    def detect_new_requests(self, data: Dict[str, Any]):
        """Detect new processes, blocks, and threads"""
        self.new_processes = []
        self.new_blocks = []
        self.new_threads = []

        # Check for new processes
        processes = data.get('processes', [])
        for proc in processes:
            pid = proc.get('pid')
            if pid and pid not in self.seen_process_pids:
                self.new_processes.append(proc)
                self.seen_process_pids.add(pid)

        # Check for new blocks
        blocks = data.get('blocks', [])
        for block in blocks:
            block_id = block.get('block_id')
            if block_id is not None and block_id not in self.seen_block_ids:
                self.new_blocks.append(block)
                self.seen_block_ids.add(block_id)

        # Check for new threads
        threads = data.get('threads', [])
        for thread in threads:
            thread_id = thread.get('thread_id')
            if thread_id is not None and thread_id not in self.seen_thread_ids:
                self.new_threads.append(thread)
                self.seen_thread_ids.add(thread_id)

    def create_snapshot(self, data: Dict[str, Any]) -> Dict[str, Any]:
        """Create a timeseries snapshot of the current VRAM state"""
        return {
            'timestamp': datetime.now().isoformat(),
            'total_bytes': data.get('total_bytes', 0),
            'used_bytes': data.get('used_bytes', 0),
            'free_bytes': data.get('free_bytes', 0),
            'reserved_bytes': data.get('reserved_bytes', 0),
            'used_percent': data.get('used_percent', 0),
            'active_blocks': data.get('active_blocks', 0),
            'free_blocks': data.get('free_blocks', 0),
            'atomic_allocations_bytes': data.get('atomic_allocations_bytes', 0),
            'fragmentation_ratio': data.get('fragmentation_ratio', 0),
            'num_processes': len(data.get('processes', [])),
            'num_threads': len(data.get('threads', [])),
            'num_blocks': len(data.get('blocks', [])),
            'processes': data.get('processes', []),
            'threads': data.get('threads', []),
            'blocks': data.get('blocks', []),
            'nsight_metrics': data.get('nsight_metrics', {}),
            'vllm_metrics': data.get('vllm_metrics', ''),
        }

    def format_bytes(self, bytes_val: int) -> str:
        """Format bytes to human readable"""
        for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
            if bytes_val < 1024.0:
                return f"{bytes_val:.2f} {unit}"
            bytes_val /= 1024.0
        return f"{bytes_val:.2f} PB"

    def print_snapshot(self, snapshot: Dict[str, Any]):
        """Print current snapshot with highlighting of new items"""
        import os
        os.system('clear' if os.name != 'nt' else 'cls')

        print("=" * 80)
        print(f"VRAM Monitor - {snapshot['timestamp']}")
        print("=" * 80)

        # Summary
        print(f"\nSummary:")
        print(f"  Total:         {self.format_bytes(snapshot['total_bytes'])}")
        print(f"  Used:          {self.format_bytes(snapshot['used_bytes'])} ({snapshot['used_percent']:.2f}%)")
        print(f"  Free:          {self.format_bytes(snapshot['free_bytes'])}")
        print(f"  Reserved:      {self.format_bytes(snapshot['reserved_bytes'])}")
        print(f"  Fragmentation: {snapshot['fragmentation_ratio']:.4f}")

        # Counts
        print(f"\nCounts:")
        print(f"  Processes:     {snapshot['num_processes']}")
        print(f"  Threads:       {snapshot['num_threads']}")
        print(f"  Blocks:        {snapshot['num_blocks']} (Active: {snapshot['active_blocks']}, Free: {snapshot['free_blocks']})")

        # New items detected
        if self.new_processes or self.new_blocks or self.new_threads:
            print(f"\n{'=' * 80}")
            print("NEW ACTIVITY DETECTED!")
            print("=" * 80)

            if self.new_processes:
                print(f"\n[NEW PROCESSES] ({len(self.new_processes)}):")
                for proc in self.new_processes:
                    print(f"  PID {proc.get('pid')}: {proc.get('name', 'unknown')} - "
                          f"Used: {self.format_bytes(proc.get('used_bytes', 0))}, "
                          f"Reserved: {self.format_bytes(proc.get('reserved_bytes', 0))}")

            if self.new_blocks:
                print(f"\n[NEW BLOCKS] ({len(self.new_blocks)}):")
                for block in self.new_blocks[:10]:  # Show first 10
                    print(f"  Block {block.get('block_id')}: {self.format_bytes(block.get('size', 0))} - "
                          f"Type: {block.get('type', 'unknown')}, "
                          f"Allocated: {block.get('allocated', False)}")
                if len(self.new_blocks) > 10:
                    print(f"  ... and {len(self.new_blocks) - 10} more new blocks")

            if self.new_threads:
                print(f"\n[NEW THREADS] ({len(self.new_threads)}):")
                for thread in self.new_threads[:10]:  # Show first 10
                    print(f"  Thread {thread.get('thread_id')}: {self.format_bytes(thread.get('allocated_bytes', 0))} - "
                          f"State: {thread.get('state', 'unknown')}")
                if len(self.new_threads) > 10:
                    print(f"  ... and {len(self.new_threads) - 10} more new threads")

        # History stats
        if len(self.history) > 1:
            print(f"\n{'=' * 80}")
            print("TIMESERIES STATISTICS")
            print("=" * 80)
            print(f"\nSnapshots collected: {len(self.history)}")

            # Calculate trends
            prev = self.history[-2]
            curr = snapshot

            used_delta = curr['used_bytes'] - prev['used_bytes']
            used_delta_str = f"+{self.format_bytes(used_delta)}" if used_delta > 0 else self.format_bytes(used_delta)

            proc_delta = curr['num_processes'] - prev['num_processes']
            proc_delta_str = f"+{proc_delta}" if proc_delta > 0 else str(proc_delta)

            print(f"\nChange since last snapshot:")
            print(f"  Used memory:   {used_delta_str}")
            print(f"  Processes:     {proc_delta_str}")
            print(f"  Fragmentation: {curr['fragmentation_ratio'] - prev['fragmentation_ratio']:+.4f}")

        print(f"\n{'=' * 80}")
        print(f"History file: {self.history_file}")
        print(f"Next update in {self.interval} seconds... (Ctrl+C to stop)")
        print("=" * 80)

    def get_statistics(self) -> Dict[str, Any]:
        """Calculate statistics from history"""
        if not self.history:
            return {}

        stats = {
            'total_snapshots': len(self.history),
            'time_range': {
                'start': self.history[0]['timestamp'],
                'end': self.history[-1]['timestamp']
            },
            'memory': {
                'avg_used_bytes': sum(s['used_bytes'] for s in self.history) / len(self.history),
                'max_used_bytes': max(s['used_bytes'] for s in self.history),
                'min_used_bytes': min(s['used_bytes'] for s in self.history),
                'avg_used_percent': sum(s['used_percent'] for s in self.history) / len(self.history),
            },
            'processes': {
                'avg_count': sum(s['num_processes'] for s in self.history) / len(self.history),
                'max_count': max(s['num_processes'] for s in self.history),
            },
            'fragmentation': {
                'avg': sum(s['fragmentation_ratio'] for s in self.history) / len(self.history),
                'max': max(s['fragmentation_ratio'] for s in self.history),
            }
        }

        return stats

    def export_csv(self, output_file: str):
        """Export history to CSV"""
        import csv

        if not self.history:
            print("No history to export")
            return

        with open(output_file, 'w', newline='') as f:
            fieldnames = [
                'timestamp', 'total_bytes', 'used_bytes', 'free_bytes',
                'reserved_bytes', 'used_percent', 'active_blocks', 'free_blocks',
                'atomic_allocations_bytes', 'fragmentation_ratio',
                'num_processes', 'num_threads', 'num_blocks'
            ]
            writer = csv.DictWriter(f, fieldnames=fieldnames, extrasaction='ignore')
            writer.writeheader()
            writer.writerows(self.history)

        print(f"Exported {len(self.history)} snapshots to {output_file}")

    def run(self):
        """Main monitoring loop"""
        print(f"Starting VRAM monitor...")
        print(f"URL: {self.url}")
        print(f"Interval: {self.interval} seconds")
        print(f"History file: {self.history_file}")
        print()

        try:
            while True:
                data = self.fetch_vram()

                if data:
                    # Detect new requests
                    self.detect_new_requests(data)

                    # Create snapshot
                    snapshot = self.create_snapshot(data)

                    # Add to history
                    self.history.append(snapshot)

                    # Print current state
                    self.print_snapshot(snapshot)

                    # Save history
                    self.save_history()
                else:
                    print(f"Failed to fetch data, retrying in {self.interval} seconds...")

                time.sleep(self.interval)

        except KeyboardInterrupt:
            print("\n\nStopping monitor...")
            self.save_history()

            # Print final statistics
            stats = self.get_statistics()
            if stats:
                print("\n" + "=" * 80)
                print("FINAL STATISTICS")
                print("=" * 80)
                print(f"\nTotal snapshots: {stats['total_snapshots']}")
                print(f"Time range: {stats['time_range']['start']} to {stats['time_range']['end']}")
                print(f"\nMemory:")
                print(f"  Average used: {self.format_bytes(int(stats['memory']['avg_used_bytes']))} ({stats['memory']['avg_used_percent']:.2f}%)")
                print(f"  Max used:     {self.format_bytes(stats['memory']['max_used_bytes'])}")
                print(f"  Min used:     {self.format_bytes(stats['memory']['min_used_bytes'])}")
                print(f"\nProcesses:")
                print(f"  Average count: {stats['processes']['avg_count']:.1f}")
                print(f"  Max count:     {stats['processes']['max_count']}")
                print(f"\nFragmentation:")
                print(f"  Average: {stats['fragmentation']['avg']:.4f}")
                print(f"  Max:     {stats['fragmentation']['max']:.4f}")
                print()


def main():
    parser = argparse.ArgumentParser(
        description='VRAM Monitor - Track GPU memory usage over time'
    )
    parser.add_argument(
        '--url',
        default='http://localhost:8080/vram',
        help='VRAM API endpoint (default: http://localhost:8080/vram)'
    )
    parser.add_argument(
        '--interval',
        type=int,
        default=5,
        help='Polling interval in seconds (default: 5)'
    )
    parser.add_argument(
        '--history-file',
        help='Path to history JSON file (default: ~/.blackbox_vram_history.json)'
    )
    parser.add_argument(
        '--export-csv',
        help='Export history to CSV file and exit'
    )
    parser.add_argument(
        '--stats',
        action='store_true',
        help='Show statistics from history and exit'
    )

    args = parser.parse_args()

    monitor = VRAMMonitor(
        url=args.url,
        interval=args.interval,
        history_file=args.history_file
    )

    if args.export_csv:
        monitor.export_csv(args.export_csv)
    elif args.stats:
        stats = monitor.get_statistics()
        if stats:
            print(json.dumps(stats, indent=2))
        else:
            print("No history data available")
    else:
        monitor.run()


if __name__ == '__main__':
    main()