#!/usr/bin/env python3
import json
import sys
import argparse
import requests
from typing import Dict, Any, Optional

def format_bytes(bytes_val: int) -> str:
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if bytes_val < 1024.0:
            return f"{bytes_val:.2f} {unit}"
        bytes_val /= 1024.0
    return f"{bytes_val:.2f} PB"

def parse_vram(data: Dict[str, Any], clear_screen: bool = False):
    if clear_screen:
        import os
        os.system('clear' if os.name != 'nt' else 'cls')

    print("===================================================")

    print("KEYS OF JSON OBJECT:")
    print(data.keys())

    print("===================================================")

    import datetime
    print("=" * 60)
    print(f"VRAM Monitor - {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 60)
    
    # Summary
    total = data.get('total_bytes', 0)
    used = data.get('used_bytes', 0)
    free = data.get('free_bytes', 0)
    reserved = data.get('reserved_bytes', 0)
    used_pct = data.get('used_percent', 0)
    
    print(f"\nSummary:")
    print(f"  Total:     {format_bytes(total)}")
    print(f"  Used:      {format_bytes(used)} ({used_pct:.2f}%)")
    print(f"  Free:      {format_bytes(free)}")
    print(f"  Reserved:  {format_bytes(reserved)}")
    
    # Blocks
    active = data.get('active_blocks', 0)  # Utilized blocks
    free_blocks = data.get('free_blocks', 0)
    frag = data.get('fragmentation_ratio', 0)
    atomic = data.get('atomic_allocations_bytes', 0)
    
    # Calculate allocated blocks from block list
    blocks = data.get('blocks', [])
    allocated_count = sum(1 for b in blocks if b.get('allocated', False))
    utilized_count = sum(1 for b in blocks if b.get('utilized', False))
    
    print(f"\nBlocks:")
    print(f"  Allocated: {allocated_count} (reserved for KV cache)")
    print(f"  Utilized:  {utilized_count} (actively storing data)")
    print(f"  Free:      {free_blocks} (allocated but not used)")
    print(f"  Fragmentation: {frag:.4f}")
    print(f"  Atomic Allocations: {format_bytes(atomic)}")
    
    # Processes
    processes = data.get('processes', [])
    if processes:
        print(f"\nProcesses ({len(processes)}):")
        print(f"  {'PID':<10} {'Name':<25} {'Used':<15} {'Reserved':<15}")
        print("  " + "-" * 65)
        for p in processes:
            print(f"  {p.get('pid', 0):<10} {p.get('name', 'unknown'):<25} "
                  f"{format_bytes(p.get('used_bytes', 0)):<15} "
                  f"{format_bytes(p.get('reserved_bytes', 0)):<15}")
    
    # Threads
    threads = data.get('threads', [])
    if threads:
        print(f"\nThreads ({len(threads)}):")
        print(f"  {'ID':<10} {'Allocated':<15} {'State':<10}")
        print("  " + "-" * 35)
        for t in threads:
            print(f"  {t.get('thread_id', 0):<10} "
                  f"{format_bytes(t.get('allocated_bytes', 0)):<15} "
                  f"{t.get('state', 'unknown'):<10}")
    
    # Blocks detail
    blocks = data.get('blocks', [])
    if blocks:
        allocated_blocks = [b for b in blocks if b.get('allocated', False)]
        free_blocks = [b for b in blocks if not b.get('allocated', False)]
        
        print(f"\nMemory Blocks ({len(blocks)} total, {len(allocated_blocks)} allocated, {len(free_blocks)} free):")
        
        if allocated_blocks:
            print(f"\n  Allocated Blocks (showing first 20 of {len(allocated_blocks)}):")
            print(f"    {'ID':<10} {'Size':<15} {'Type':<15}")
            print("    " + "-" * 40)
            for b in allocated_blocks[:20]:
                print(f"    {b.get('block_id', 0):<10} "
                      f"{format_bytes(b.get('size', 0)):<15} "
                      f"{b.get('type', 'unknown'):<15}")
            if len(allocated_blocks) > 20:
                print(f"    ... and {len(allocated_blocks) - 20} more allocated blocks")
        
        if free_blocks:
            print(f"\n  Free Blocks (showing first 20 of {len(free_blocks)}):")
            print(f"    {'ID':<10} {'Size':<15} {'Type':<15}")
            print("    " + "-" * 40)
            for b in free_blocks[:20]:
                print(f"    {b.get('block_id', 0):<10} "
                      f"{format_bytes(b.get('size', 0)):<15} "
                      f"{b.get('type', 'unknown'):<15}")
            if len(free_blocks) > 20:
                print(f"    ... and {len(free_blocks) - 20} more free blocks")
    
    # Nsight Compute Metrics
    nsight_metrics = data.get('nsight_metrics', {})
    if nsight_metrics:
        print(f"\nNsight Compute Metrics:")
        print("  " + "-" * 58)
        for pid, metrics in nsight_metrics.items():
            if isinstance(metrics, dict) and metrics.get('available', False):
                print(f"\n  PID {pid}:")
                print(f"    Atomic Operations: {metrics.get('atomic_operations', 0):,}")
                print(f"    Threads per Block: {metrics.get('threads_per_block', 0)}")
                print(f"    Blocks per SM: {metrics.get('blocks_per_sm', 0)}")
                print(f"    Shared Memory Usage: {format_bytes(metrics.get('shared_memory_usage', 0))}")
                print(f"    Occupancy: {metrics.get('occupancy', 0.0):.2f}%")
            elif isinstance(metrics, dict):
                print(f"\n  PID {pid}: (No metrics available - process may not be running CUDA kernels)")
    
    # vLLM Metrics
    vllm_metrics = data.get('vllm_metrics', '')
    if vllm_metrics:
        print(f"\nvLLM Metrics:")
        print("  " + "-" * 58)
        for line in vllm_metrics.split('\n'):
            if line.strip() and not line.strip().startswith('#'):
                print(f"  {line}")
    
    print("\n" + "=" * 60)

def fetch_vram_stream(url: str = "http://localhost:8080/vram"):
    import time
    try:
        base_url = url.rstrip('/').replace('/stream', '')
        # Poll the regular endpoint since SSE implementation is broken
        while True:
            try:
                data = fetch_vram(base_url)
                if data:
                    yield data
                time.sleep(0.5)
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"Error in stream: {e}", file=sys.stderr)
                time.sleep(1)
    except KeyboardInterrupt:
        pass

def fetch_vram(url: str = "http://localhost:8080/vram") -> Optional[Dict[str, Any]]:
    try:
        response = requests.get(url, timeout=60)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error fetching from {url}: {e}", file=sys.stderr)
        return None

def main():
    parser = argparse.ArgumentParser(description='Parse VRAM monitor JSON output')
    parser.add_argument('input', nargs='?', type=str, default=None,
                       help='JSON input file (default: fetch from server)')
    parser.add_argument('--url', default='http://localhost:8080/vram',
                       help='Server URL (default: http://localhost:8080/vram)')
    parser.add_argument('--stream', action='store_true',
                       help='Stream updates continuously')
    
    args = parser.parse_args()
    
    # Handle case where user types --stream true (ignore "true" as input)
    if args.input and args.input.lower() in ['true', 'false'] and args.stream:
        args.input = None
    
    if args.input is None:
        # Fetch from server
        if args.stream:
            try:
                for data in fetch_vram_stream(args.url):
                    if data:
                        parse_vram(data, clear_screen=True)
                    else:
                        print("No data received", file=sys.stderr)
            except KeyboardInterrupt:
                print("\nStopped.")
        else:
            data = fetch_vram(args.url)
            if data:
                parse_vram(data)
            else:
                sys.exit(1)
    else:
        # Parse from input
        if args.stream:
            buffer = ""
            for line in args.input:
                buffer += line
                if line.strip() == "" and buffer.strip().startswith("data: "):
                    json_str = buffer.strip()[6:]  # Remove "data: " prefix
                    try:
                        data = json.loads(json_str)
                        parse_vram(data)
                        buffer = ""
                    except json.JSONDecodeError as e:
                        print(f"Error parsing JSON: {e}", file=sys.stderr)
                        buffer = ""
        else:
            try:
                with open(args.input, 'r') as f:
                    data = json.load(f)
                    parse_vram(data)
            except (json.JSONDecodeError, FileNotFoundError) as e:
                print(f"Error parsing JSON: {e}", file=sys.stderr)
                sys.exit(1)

if __name__ == '__main__':
    main()

