"""
FastAPI backend for VRAM monitoring dashboard
"""
from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel
from typing import List, Optional, Dict, Any
from datetime import datetime, timedelta
from sqlalchemy import func, desc
from sqlalchemy.orm import Session
import json
import os
import requests
import threading
import time
import sys

from database import (
    init_db, get_session,
    Node, VRAMSnapshot, Process, Thread, Block, NsightMetric
)


# ============================================================================
# Embedded Data Collector
# ============================================================================

class NodeCollector:
    """Collector for a single node - runs in its own thread"""
    def __init__(self, node_id: int, node_name: str, host: str, port: int, interval: int = 5):
        self.node_id = node_id
        self.node_name = node_name
        self.host = host
        self.port = port
        self.vram_url = f"http://{host}:{port}/vram"
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

    def submit_to_database(self, data: Dict[str, Any]) -> bool:
        """Submit data directly to database"""
        try:
            db = get_session()
            try:
                # Calculate derived metrics
                # New API returns: allocated_blocks (total), utilized_blocks (used), free_blocks (allocated but not used)
                allocated_blocks = data.get("allocated_blocks", 0)
                utilized_blocks = data.get("utilized_blocks", 0)
                free_blocks = data.get("free_blocks", 0)

                total_bytes = data.get("total_bytes", 0)
                used_bytes = data.get("used_bytes", 0)

                kv_cache_util = (utilized_blocks / allocated_blocks * 100) if allocated_blocks > 0 else 0
                vllm_mem_util = (used_bytes / total_bytes * 100) if total_bytes > 0 else 0

                # Create snapshot
                db_snapshot = VRAMSnapshot(
                    node_id=self.node_id,
                    timestamp=datetime.now(),
                    total_bytes=total_bytes,
                    used_bytes=used_bytes,
                    free_bytes=data.get("free_bytes", 0),
                    reserved_bytes=data.get("reserved_bytes", 0),
                    used_percent=data.get("used_percent", 0.0),
                    allocated_blocks=allocated_blocks,  # Total allocated blocks for KV cache
                    free_blocks=free_blocks,  # Allocated but not utilized
                    utilized_blocks=utilized_blocks,  # Actually being used
                    atomic_allocations_bytes=data.get("atomic_allocations_bytes", 0),
                    fragmentation_ratio=data.get("fragmentation_ratio", 0.0),
                    num_processes=len(data.get("processes", [])),
                    num_threads=len(data.get("threads", [])),
                    num_blocks=len(data.get("blocks", [])),
                    kv_cache_utilization=kv_cache_util,
                    vllm_memory_utilization=vllm_mem_util,
                    memory_fragmentation=data.get("fragmentation_ratio", 0.0),
                    vllm_metrics=data.get("vllm_metrics", "")
                )

                # Add processes
                for proc in data.get("processes", []):
                    db.add(Process(
                        snapshot=db_snapshot,
                        pid=proc.get("pid"),
                        name=proc.get("name", "unknown"),
                        used_bytes=proc.get("used_bytes", 0),
                        reserved_bytes=proc.get("reserved_bytes", 0)
                    ))

                # Add threads
                for thread in data.get("threads", []):
                    db.add(Thread(
                        snapshot=db_snapshot,
                        thread_id=thread.get("thread_id"),
                        allocated_bytes=thread.get("allocated_bytes", 0),
                        state=thread.get("state", "unknown")
                    ))

                # Add blocks
                for block in data.get("blocks", []):
                    db.add(Block(
                        snapshot=db_snapshot,
                        block_id=block.get("block_id"),
                        size=block.get("size", 0),
                        block_type=block.get("type", "unknown"),
                        allocated=1 if block.get("allocated", False) else 0,
                        utilized=1 if block.get("utilized", False) else 0
                    ))

                # Add nsight metrics
                for pid_str, metrics in data.get("nsight_metrics", {}).items():
                    if isinstance(metrics, dict):
                        db.add(NsightMetric(
                            snapshot=db_snapshot,
                            pid=int(pid_str),
                            available=1 if metrics.get('available', False) else 0,
                            atomic_operations=metrics.get('atomic_operations'),
                            threads_per_block=metrics.get('threads_per_block'),
                            blocks_per_sm=metrics.get('blocks_per_sm'),
                            shared_memory_usage=metrics.get('shared_memory_usage'),
                            occupancy=metrics.get('occupancy')
                        ))

                # Update node's last_seen
                node = db.query(Node).filter(Node.id == self.node_id).first()
                if node:
                    node.last_seen = datetime.now()

                db.add(db_snapshot)
                db.commit()
                return True

            except Exception as e:
                db.rollback()
                print(f"[{self.node_name}] Database error: {e}", file=sys.stderr)
                return False
            finally:
                db.close()

        except Exception as e:
            print(f"[{self.node_name}] Error submitting to database: {e}", file=sys.stderr)
            return False

    def collect_loop(self):
        """Main collection loop (runs in thread)"""
        print(f"[{self.node_name}] Starting collector (interval: {self.interval}s, endpoint: {self.vram_url})")

        while self.running:
            # Fetch data
            data = self.fetch_vram_data()

            if data:
                # Submit to database
                success = self.submit_to_database(data)

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
        if not self.running:
            self.running = True
            self.thread = threading.Thread(target=self.collect_loop, daemon=True)
            self.thread.start()

    def stop(self):
        """Stop collection thread"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)


class CollectorManager:
    """Manages all node collectors"""
    def __init__(self, interval: int = 5):
        self.interval = interval
        self.collectors: Dict[int, NodeCollector] = {}
        self._lock = threading.Lock()

    def start_collector(self, node_id: int, node_name: str, host: str, port: int):
        """Start collector for a node"""
        with self._lock:
            if node_id in self.collectors:
                print(f"Collector for node '{node_name}' (ID: {node_id}) already running")
                return

            collector = NodeCollector(
                node_id=node_id,
                node_name=node_name,
                host=host,
                port=port,
                interval=self.interval
            )
            collector.start()
            self.collectors[node_id] = collector
            print(f"Started collector for '{node_name}' (ID: {node_id})")

    def stop_collector(self, node_id: int):
        """Stop collector for a node"""
        with self._lock:
            if node_id in self.collectors:
                collector = self.collectors[node_id]
                collector.stop()
                del self.collectors[node_id]
                print(f"Stopped collector for node ID {node_id}")

    def restart_collector(self, node_id: int, node_name: str, host: str, port: int):
        """Restart collector for a node (used when settings change)"""
        self.stop_collector(node_id)
        self.start_collector(node_id, node_name, host, port)

    def initialize_from_database(self):
        """Load all enabled nodes from database and start collectors"""
        db = get_session()
        try:
            nodes = db.query(Node).filter(Node.enabled == True).all()
            for node in nodes:
                self.start_collector(node.id, node.name, node.host, node.port)

            if nodes:
                print(f"Initialized {len(nodes)} collector(s) from database")
        finally:
            db.close()

    def stop_all(self):
        """Stop all collectors"""
        with self._lock:
            for collector in list(self.collectors.values()):
                collector.stop()
            self.collectors.clear()
            print("Stopped all collectors")


# Global collector manager instance
collector_manager = CollectorManager(interval=5)


app = FastAPI(
    title="Blackbox VRAM Monitor API",
    description="REST API for GPU memory monitoring and analytics",
    version="1.0.0"
)

# Enable CORS for frontend (must be added before routes and static files)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # In production, specify your frontend URL
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
    expose_headers=["*"],
)

# Mount static files for dashboard
static_dir = os.path.join(os.path.dirname(__file__), "static")
if os.path.exists(static_dir):
    app.mount("/static", StaticFiles(directory=static_dir), name="static")


# Pydantic models for API
class NodeCreate(BaseModel):
    name: str
    host: str
    port: int = 6767


class NodeResponse(BaseModel):
    id: int
    name: str
    host: str
    port: int
    enabled: bool
    created_at: datetime
    last_seen: Optional[datetime]

    class Config:
        from_attributes = True


class ProcessData(BaseModel):
    pid: int
    name: str
    used_bytes: int
    reserved_bytes: int


class ThreadData(BaseModel):
    thread_id: int
    allocated_bytes: int
    state: str


class BlockData(BaseModel):
    block_id: int
    size: int
    type: str
    allocated: bool
    utilized: Optional[bool] = None


class NsightMetricData(BaseModel):
    pid: int
    available: bool
    atomic_operations: Optional[int] = None
    threads_per_block: Optional[int] = None
    blocks_per_sm: Optional[int] = None
    shared_memory_usage: Optional[int] = None
    occupancy: Optional[float] = None


class VRAMSnapshotCreate(BaseModel):
    node_id: Optional[int] = None  # Will be required once migration is complete
    timestamp: Optional[datetime] = None
    total_bytes: int
    used_bytes: int
    free_bytes: int
    reserved_bytes: int
    used_percent: float
    allocated_blocks: int  # Total allocated blocks for KV cache
    free_blocks: int  # Allocated but not utilized
    atomic_allocations_bytes: int
    fragmentation_ratio: float
    processes: List[ProcessData] = []
    threads: List[ThreadData] = []
    blocks: List[BlockData] = []
    nsight_metrics: Dict[str, Any] = {}
    vllm_metrics: Optional[str] = None


class VRAMSnapshotResponse(BaseModel):
    id: int
    timestamp: datetime
    total_bytes: int
    used_bytes: int
    free_bytes: int
    reserved_bytes: int
    used_percent: float
    allocated_blocks: int  # Total allocated blocks for KV cache
    free_blocks: int  # Allocated but not utilized
    utilized_blocks: Optional[int] = None  # Actually being used
    atomic_allocations_bytes: int
    fragmentation_ratio: float
    num_processes: int
    num_threads: int
    num_blocks: int
    kv_cache_utilization: Optional[float] = None
    vllm_memory_utilization: Optional[float] = None
    memory_fragmentation: Optional[float] = None

    class Config:
        from_attributes = True


class TimeseriesDataPoint(BaseModel):
    timestamp: datetime
    value: float


class StatsSummary(BaseModel):
    total_snapshots: int
    time_range_start: Optional[datetime]
    time_range_end: Optional[datetime]
    avg_used_bytes: float
    max_used_bytes: int
    min_used_bytes: int
    avg_used_percent: float
    avg_fragmentation: float
    max_fragmentation: float


# Initialize database and collectors on startup
@app.on_event("startup")
async def startup_event():
    init_db()
    # Start collectors for all enabled nodes
    collector_manager.initialize_from_database()


# Cleanup on shutdown
@app.on_event("shutdown")
async def shutdown_event():
    collector_manager.stop_all()


# API Endpoints

@app.get("/")
async def root():
    """Root endpoint - redirect to dashboard"""
    from fastapi.responses import RedirectResponse
    return RedirectResponse(url="/static/index.html")


@app.get("/api")
async def api_info():
    """API information"""
    return {
        "name": "Blackbox VRAM Monitor API",
        "version": "1.0.0",
        "endpoints": {
            "dashboard": "/static/index.html",
            "nodes": "/api/nodes",
            "snapshots": "/api/snapshots",
            "latest": "/api/snapshots/latest",
            "timeseries": "/api/timeseries/{metric}",
            "stats": "/api/stats",
            "processes": "/api/processes",
            "submit": "POST /api/snapshots"
        }
    }


# Node Management Endpoints

@app.post("/api/nodes", response_model=NodeResponse)
async def create_node(node: NodeCreate):
    """Add a new GPU node to monitor"""
    db = get_session()
    try:
        # Check if host:port already exists
        if db.query(Node).filter(Node.host == node.host, Node.port == node.port).first():
            raise HTTPException(status_code=400, detail=f"Node {node.host}:{node.port} already exists")

        # Check if name already exists
        if db.query(Node).filter(Node.name == node.name).first():
            raise HTTPException(status_code=400, detail=f"Node with name '{node.name}' already exists")

        db_node = Node(
            name=node.name,
            host=node.host,
            port=node.port,
            enabled=True
        )

        db.add(db_node)
        db.commit()
        db.refresh(db_node)

        # Auto-start collector for the new node
        collector_manager.start_collector(db_node.id, db_node.name, db_node.host, db_node.port)

        return db_node

    except HTTPException:
        raise
    except Exception as e:
        db.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db.close()


@app.get("/api/nodes", response_model=List[NodeResponse])
async def list_nodes():
    """List all GPU nodes"""
    db = get_session()
    try:
        nodes = db.query(Node).order_by(Node.name).all()
        return nodes
    finally:
        db.close()


@app.get("/api/nodes/{node_id}", response_model=NodeResponse)
async def get_node(node_id: int):
    """Get a specific node"""
    db = get_session()
    try:
        node = db.query(Node).filter(Node.id == node_id).first()
        if not node:
            raise HTTPException(status_code=404, detail="Node not found")
        return node
    finally:
        db.close()


@app.put("/api/nodes/{node_id}", response_model=NodeResponse)
async def update_node(node_id: int, enabled: Optional[bool] = None,
                     name: Optional[str] = None, host: Optional[str] = None,
                     port: Optional[int] = None):
    """Update a node (enable/disable or change settings)"""
    db = get_session()
    try:
        node = db.query(Node).filter(Node.id == node_id).first()
        if not node:
            raise HTTPException(status_code=404, detail="Node not found")

        # Track if settings that affect the collector changed
        settings_changed = False
        enabled_changed = False

        if enabled is not None and node.enabled != enabled:
            node.enabled = enabled
            enabled_changed = True
        if name is not None:
            node.name = name
        if host is not None and node.host != host:
            node.host = host
            settings_changed = True
        if port is not None and node.port != port:
            node.port = port
            settings_changed = True

        db.commit()
        db.refresh(node)

        # Manage collector based on changes
        if enabled_changed:
            if node.enabled:
                # Node was enabled - start collector
                collector_manager.start_collector(node.id, node.name, node.host, node.port)
            else:
                # Node was disabled - stop collector
                collector_manager.stop_collector(node.id)
        elif settings_changed and node.enabled:
            # Settings changed for enabled node - restart collector
            collector_manager.restart_collector(node.id, node.name, node.host, node.port)

        return node

    except HTTPException:
        raise
    except Exception as e:
        db.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db.close()


@app.delete("/api/nodes/{node_id}")
async def delete_node(node_id: int):
    """Delete a node and all its data"""
    db = get_session()
    try:
        node = db.query(Node).filter(Node.id == node_id).first()
        if not node:
            raise HTTPException(status_code=404, detail="Node not found")

        node_name = node.name
        db.delete(node)
        db.commit()

        # Stop collector for deleted node
        collector_manager.stop_collector(node_id)

        return {"message": f"Node '{node_name}' deleted successfully"}

    except HTTPException:
        raise
    except Exception as e:
        db.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db.close()


# Snapshot Endpoints

@app.post("/api/snapshots", response_model=VRAMSnapshotResponse)
async def create_snapshot(snapshot: VRAMSnapshotCreate):
    """Submit a new VRAM snapshot"""
    db = get_session()
    try:
        # Get or create default node if node_id not provided (for backward compatibility)
        node_id = snapshot.node_id
        if node_id is None:
            default_node = db.query(Node).filter(Node.name == "default").first()
            if not default_node:
                default_node = Node(
                    name="default",
                    host="localhost",
                    port=6767,
                    enabled=True
                )
                db.add(default_node)
                db.flush()  # Get the ID without committing
            node_id = default_node.id

            # Update last_seen
            default_node.last_seen = datetime.now()
        else:
            # Update last_seen for specified node
            node = db.query(Node).filter(Node.id == node_id).first()
            if not node:
                raise HTTPException(status_code=404, detail=f"Node with id {node_id} not found")
            node.last_seen = datetime.now()

        # Calculate derived metrics
        utilized_blocks = sum(1 for b in snapshot.blocks if b.utilized)
        kv_cache_util = (utilized_blocks / snapshot.active_blocks * 100) if snapshot.active_blocks > 0 else 0
        vllm_mem_util = (snapshot.used_bytes / snapshot.total_bytes * 100) if snapshot.total_bytes > 0 else 0

        # Create snapshot
        db_snapshot = VRAMSnapshot(
            node_id=node_id,
            timestamp=snapshot.timestamp or datetime.now(),
            total_bytes=snapshot.total_bytes,
            used_bytes=snapshot.used_bytes,
            free_bytes=snapshot.free_blocks,
            reserved_bytes=snapshot.reserved_bytes,
            used_percent=snapshot.used_percent,
            active_blocks=snapshot.active_blocks,
            free_blocks=snapshot.free_blocks,
            utilized_blocks=utilized_blocks,
            atomic_allocations_bytes=snapshot.atomic_allocations_bytes,
            fragmentation_ratio=snapshot.fragmentation_ratio,
            num_processes=len(snapshot.processes),
            num_threads=len(snapshot.threads),
            num_blocks=len(snapshot.blocks),
            kv_cache_utilization=kv_cache_util,
            vllm_memory_utilization=vllm_mem_util,
            memory_fragmentation=snapshot.fragmentation_ratio,
            vllm_metrics=snapshot.vllm_metrics
        )

        # Add processes
        for proc in snapshot.processes:
            db.add(Process(
                snapshot=db_snapshot,
                pid=proc.pid,
                name=proc.name,
                used_bytes=proc.used_bytes,
                reserved_bytes=proc.reserved_bytes
            ))

        # Add threads
        for thread in snapshot.threads:
            db.add(Thread(
                snapshot=db_snapshot,
                thread_id=thread.thread_id,
                allocated_bytes=thread.allocated_bytes,
                state=thread.state
            ))

        # Add blocks
        for block in snapshot.blocks:
            db.add(Block(
                snapshot=db_snapshot,
                block_id=block.block_id,
                size=block.size,
                block_type=block.type,
                allocated=1 if block.allocated else 0,
                utilized=1 if block.utilized else 0
            ))

        # Add nsight metrics
        for pid_str, metrics in snapshot.nsight_metrics.items():
            if isinstance(metrics, dict):
                db.add(NsightMetric(
                    snapshot=db_snapshot,
                    pid=int(pid_str),
                    available=1 if metrics.get('available', False) else 0,
                    atomic_operations=metrics.get('atomic_operations'),
                    threads_per_block=metrics.get('threads_per_block'),
                    blocks_per_sm=metrics.get('blocks_per_sm'),
                    shared_memory_usage=metrics.get('shared_memory_usage'),
                    occupancy=metrics.get('occupancy')
                ))

        db.add(db_snapshot)
        db.commit()
        db.refresh(db_snapshot)

        return db_snapshot

    except Exception as e:
        db.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db.close()


@app.get("/api/snapshots", response_model=List[VRAMSnapshotResponse])
async def get_snapshots(
    limit: int = Query(100, le=1000),
    offset: int = Query(0, ge=0),
    start_time: Optional[datetime] = None,
    end_time: Optional[datetime] = None
):
    """Get paginated list of VRAM snapshots"""
    db = get_session()
    try:
        query = db.query(VRAMSnapshot).order_by(desc(VRAMSnapshot.timestamp))

        if start_time:
            query = query.filter(VRAMSnapshot.timestamp >= start_time)
        if end_time:
            query = query.filter(VRAMSnapshot.timestamp <= end_time)

        snapshots = query.offset(offset).limit(limit).all()
        return snapshots

    finally:
        db.close()


@app.get("/api/snapshots/latest", response_model=VRAMSnapshotResponse)
async def get_latest_snapshot(node_id: Optional[int] = Query(None, description="Filter by node ID")):
    """Get the most recent VRAM snapshot"""
    db = get_session()
    try:
        query = db.query(VRAMSnapshot)

        # Filter by node if specified
        if node_id is not None:
            query = query.filter(VRAMSnapshot.node_id == node_id)

        snapshot = query.order_by(desc(VRAMSnapshot.timestamp)).first()

        if not snapshot:
            raise HTTPException(status_code=404, detail="No snapshots found")

        return snapshot

    finally:
        db.close()


@app.get("/api/snapshots/{snapshot_id}")
async def get_snapshot_detail(snapshot_id: int):
    """Get detailed snapshot with all related data"""
    db = get_session()
    try:
        snapshot = db.query(VRAMSnapshot).filter(
            VRAMSnapshot.id == snapshot_id
        ).first()

        if not snapshot:
            raise HTTPException(status_code=404, detail="Snapshot not found")

        # Build detailed response
        return {
            "id": snapshot.id,
            "timestamp": snapshot.timestamp,
            "total_bytes": snapshot.total_bytes,
            "used_bytes": snapshot.used_bytes,
            "free_bytes": snapshot.free_bytes,
            "reserved_bytes": snapshot.reserved_bytes,
            "used_percent": snapshot.used_percent,
            "active_blocks": snapshot.active_blocks,
            "free_blocks": snapshot.free_blocks,
            "atomic_allocations_bytes": snapshot.atomic_allocations_bytes,
            "fragmentation_ratio": snapshot.fragmentation_ratio,
            "processes": [
                {
                    "pid": p.pid,
                    "name": p.name,
                    "used_bytes": p.used_bytes,
                    "reserved_bytes": p.reserved_bytes
                }
                for p in snapshot.processes
            ],
            "threads": [
                {
                    "thread_id": t.thread_id,
                    "allocated_bytes": t.allocated_bytes,
                    "state": t.state
                }
                for t in snapshot.threads
            ],
            "blocks": [
                {
                    "block_id": b.block_id,
                    "size": b.size,
                    "type": b.block_type,
                    "allocated": bool(b.allocated),
                    "utilized": bool(b.utilized)
                }
                for b in snapshot.blocks
            ],
            "nsight_metrics": {
                str(n.pid): {
                    "available": bool(n.available),
                    "atomic_operations": n.atomic_operations,
                    "threads_per_block": n.threads_per_block,
                    "blocks_per_sm": n.blocks_per_sm,
                    "shared_memory_usage": n.shared_memory_usage,
                    "occupancy": n.occupancy
                }
                for n in snapshot.nsight_metrics
            },
            "vllm_metrics": snapshot.vllm_metrics
        }

    finally:
        db.close()


@app.get("/api/timeseries/{metric}", response_model=List[TimeseriesDataPoint])
async def get_timeseries(
    metric: str,
    duration: int = Query(3600, description="Duration in seconds"),
    interval: Optional[int] = Query(None, description="Sampling interval in seconds"),
    node_id: Optional[int] = Query(None, description="Filter by node ID")
):
    """
    Get timeseries data for a specific metric

    Supported metrics:
    - used_bytes
    - used_percent
    - fragmentation_ratio
    - num_processes
    - num_threads
    - active_blocks
    """
    db = get_session()
    try:
        # Calculate time range
        end_time = datetime.now()
        start_time = end_time - timedelta(seconds=duration)

        # Query snapshots in time range
        query = db.query(VRAMSnapshot).filter(
            VRAMSnapshot.timestamp >= start_time,
            VRAMSnapshot.timestamp <= end_time
        )

        # Filter by node if specified
        if node_id is not None:
            query = query.filter(VRAMSnapshot.node_id == node_id)

        query = query.order_by(VRAMSnapshot.timestamp)

        snapshots = query.all()

        if not snapshots:
            return []

        # Extract metric values
        metric_map = {
            'used_bytes': lambda s: s.used_bytes,
            'used_percent': lambda s: s.used_percent,
            'fragmentation_ratio': lambda s: s.fragmentation_ratio,
            'num_processes': lambda s: s.num_processes,
            'num_threads': lambda s: s.num_threads,
            'allocated_blocks': lambda s: s.allocated_blocks,  # Total allocated blocks
            'utilized_blocks': lambda s: s.utilized_blocks or 0,  # Actually used blocks
            'free_blocks': lambda s: s.free_blocks,  # Allocated but not used
            'reserved_bytes': lambda s: s.reserved_bytes,
            'kv_cache_utilization': lambda s: s.kv_cache_utilization or 0,
            'vllm_memory_utilization': lambda s: s.vllm_memory_utilization or 0,
            'memory_fragmentation': lambda s: s.memory_fragmentation or 0,
        }

        if metric not in metric_map:
            raise HTTPException(
                status_code=400,
                detail=f"Unknown metric: {metric}. Supported: {list(metric_map.keys())}"
            )

        extractor = metric_map[metric]

        # Sample data if interval specified
        if interval:
            sampled = []
            last_time = None
            for snapshot in snapshots:
                if last_time is None or (snapshot.timestamp - last_time).total_seconds() >= interval:
                    sampled.append(snapshot)
                    last_time = snapshot.timestamp
            snapshots = sampled

        return [
            TimeseriesDataPoint(
                timestamp=snapshot.timestamp,
                value=float(extractor(snapshot))
            )
            for snapshot in snapshots
        ]

    finally:
        db.close()


@app.get("/api/stats", response_model=StatsSummary)
async def get_stats(
    duration: Optional[int] = Query(None, description="Duration in seconds, None for all time")
):
    """Get summary statistics"""
    db = get_session()
    try:
        query = db.query(VRAMSnapshot)

        if duration:
            start_time = datetime.now() - timedelta(seconds=duration)
            query = query.filter(VRAMSnapshot.timestamp >= start_time)

        snapshots = query.all()

        if not snapshots:
            raise HTTPException(status_code=404, detail="No data available")

        return StatsSummary(
            total_snapshots=len(snapshots),
            time_range_start=min(s.timestamp for s in snapshots),
            time_range_end=max(s.timestamp for s in snapshots),
            avg_used_bytes=sum(s.used_bytes for s in snapshots) / len(snapshots),
            max_used_bytes=max(s.used_bytes for s in snapshots),
            min_used_bytes=min(s.used_bytes for s in snapshots),
            avg_used_percent=sum(s.used_percent for s in snapshots) / len(snapshots),
            avg_fragmentation=sum(s.fragmentation_ratio for s in snapshots) / len(snapshots),
            max_fragmentation=max(s.fragmentation_ratio for s in snapshots)
        )

    finally:
        db.close()


@app.get("/api/processes")
async def get_process_history(
    duration: int = Query(3600, description="Duration in seconds"),
    node_id: Optional[int] = Query(None, description="Filter by node ID")
):
    """Get process activity history"""
    db = get_session()
    try:
        start_time = datetime.now() - timedelta(seconds=duration)

        # Get all processes in time range with their snapshots
        query = db.query(Process, VRAMSnapshot.timestamp).join(
            VRAMSnapshot
        ).filter(
            VRAMSnapshot.timestamp >= start_time
        )

        # Filter by node if specified
        if node_id is not None:
            query = query.filter(VRAMSnapshot.node_id == node_id)

        processes = query.order_by(VRAMSnapshot.timestamp).all()

        # Group by PID
        process_history = {}
        for proc, timestamp in processes:
            if proc.pid not in process_history:
                process_history[proc.pid] = {
                    'pid': proc.pid,
                    'name': proc.name,
                    'history': []
                }
            process_history[proc.pid]['history'].append({
                'timestamp': timestamp,
                'used_bytes': proc.used_bytes,
                'reserved_bytes': proc.reserved_bytes
            })

        return list(process_history.values())

    finally:
        db.close()


@app.delete("/api/snapshots")
async def cleanup_old_snapshots(
    older_than_days: int = Query(30, description="Delete snapshots older than N days")
):
    """Delete old snapshots to free up space"""
    db = get_session()
    try:
        cutoff_time = datetime.now() - timedelta(days=older_than_days)

        deleted = db.query(VRAMSnapshot).filter(
            VRAMSnapshot.timestamp < cutoff_time
        ).delete()

        db.commit()

        return {
            "deleted": deleted,
            "cutoff_time": cutoff_time
        }

    except Exception as e:
        db.rollback()
        raise HTTPException(status_code=500, detail=str(e))
    finally:
        db.close()


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8001)