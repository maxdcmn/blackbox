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

def parse_vram(data: Dict[str, Any]):
    print("=" * 60)
    print("VRAM Monitor")
    print("=" * 60)
    
    # Summary
    total = data.get('total_bytes', 0)
    used = data.get('used_bytes', 0)
    free = data.get('free_bytes', 0)
    reserved = data.get('reserved_bytes', 0)
    used_pct = data.get('used_percent', 0)
    
    print(f"\n📊 Summary:")
    print(f"  Total:     {format_bytes(total)}")
    print(f"  Used:      {format_bytes(used)} ({used_pct:.2f}%)")
    print(f"  Free:      {format_bytes(free)}")
    print(f"  Reserved:  {format_bytes(reserved)}")
    
    # Blocks
    active = data.get('active_blocks', 0)
    free_blocks = data.get('free_blocks', 0)
    frag = data.get('fragmentation_ratio', 0)
    atomic = data.get('atomic_allocations_bytes', 0)
    
    print(f"\n🔲 Blocks:")
    print(f"  Active: {active}")
    print(f"  Free:   {free_blocks}")
    print(f"  Fragmentation: {frag:.4f}")
    print(f"  Atomic Allocations: {format_bytes(atomic)}")
    
    # Processes
    processes = data.get('processes', [])
    if processes:
        print(f"\n🖥️  Processes ({len(processes)}):")
        print(f"  {'PID':<10} {'Name':<25} {'Used':<15} {'Reserved':<15}")
        print("  " + "-" * 65)
        for p in processes:
            print(f"  {p.get('pid', 0):<10} {p.get('name', 'unknown'):<25} "
                  f"{format_bytes(p.get('used_bytes', 0)):<15} "
                  f"{format_bytes(p.get('reserved_bytes', 0)):<15}")
    
    # Threads
    threads = data.get('threads', [])
    if threads:
        print(f"\n🧵 Threads ({len(threads)}):")
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
        
        print(f"\n📦 Memory Blocks ({len(blocks)} total, {len(allocated_blocks)} allocated, {len(free_blocks)} free):")
        
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
    
    # vLLM Metrics
    vllm_metrics = data.get('vllm_metrics', '')
    if vllm_metrics:
        print(f"\n🤖 vLLM Metrics:")
        print("  " + "-" * 58)
        for line in vllm_metrics.split('\n'):
            if line.strip() and not line.strip().startswith('#'):
                print(f"  {line}")
    
    print("\n" + "=" * 60)

def fetch_vram_stream(url: str = "http://localhost:8080/vram"):
    try:
        response = requests.get(f"{url}/stream", stream=True, timeout=None)
        response.raise_for_status()
        buffer = ""
        for line in response.iter_lines(decode_unicode=True):
            if line:
                buffer += line + "\n"
            elif buffer.strip().startswith("data: "):
                json_str = buffer.strip()[6:]  # Remove "data: " prefix
                try:
                    data = json.loads(json_str)
                    yield data
                    buffer = ""
                except json.JSONDecodeError:
                    buffer = ""
    except requests.exceptions.RequestException as e:
        print(f"Error fetching from {url}: {e}", file=sys.stderr)

def fetch_vram(url: str = "http://localhost:8080/vram") -> Optional[Dict[str, Any]]:
    try:
        response = requests.get(url, timeout=5)
        response.raise_for_status()
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error fetching from {url}: {e}", file=sys.stderr)
        return None

def main():
    parser = argparse.ArgumentParser(description='Parse VRAM monitor JSON output')
    parser.add_argument('input', nargs='?', type=argparse.FileType('r'), default=None,
                       help='JSON input file (default: fetch from server)')
    parser.add_argument('--url', default='http://localhost:8080/vram',
                       help='Server URL (default: http://localhost:8080/vram)')
    parser.add_argument('--stream', action='store_true',
                       help='Stream updates continuously')
    
    args = parser.parse_args()
    
    if args.input is None:
        # Fetch from server
        if args.stream:
            try:
                for data in fetch_vram_stream(args.url):
                    parse_vram(data)
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
                data = json.load(args.input)
                parse_vram(data)
            except json.JSONDecodeError as e:
                print(f"Error parsing JSON: {e}", file=sys.stderr)
                sys.exit(1)

if __name__ == '__main__':
    main()

