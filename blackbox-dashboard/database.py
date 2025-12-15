"""
Database models and setup for VRAM monitoring
"""
from sqlalchemy import create_engine, Column, Integer, Float, String, DateTime, JSON, ForeignKey, Boolean
from sqlalchemy.ext.declarative import declarative_base
from sqlalchemy.orm import sessionmaker, relationship
from datetime import datetime
import os

Base = declarative_base()


class Node(Base):
    """GPU node/machine being monitored"""
    __tablename__ = 'nodes'

    id = Column(Integer, primary_key=True, autoincrement=True)
    name = Column(String, nullable=False, unique=True)  # User-friendly name
    host = Column(String, nullable=False)  # IP or hostname
    port = Column(Integer, default=6767)
    enabled = Column(Boolean, default=True)  # Can disable without deleting
    created_at = Column(DateTime, default=datetime.now)
    last_seen = Column(DateTime, nullable=True)  # Last successful data fetch

    # Relationships
    snapshots = relationship("VRAMSnapshot", back_populates="node", cascade="all, delete-orphan")


class VRAMSnapshot(Base):
    """Main timeseries snapshot of VRAM metrics"""
    __tablename__ = 'vram_snapshots'

    id = Column(Integer, primary_key=True, autoincrement=True)
    node_id = Column(Integer, ForeignKey('nodes.id'), index=True, nullable=False)
    timestamp = Column(DateTime, default=datetime.now, index=True)

    # Memory metrics
    total_bytes = Column(Integer)
    used_bytes = Column(Integer)
    free_bytes = Column(Integer)
    reserved_bytes = Column(Integer)
    used_percent = Column(Float)

    # Block metrics
    allocated_blocks = Column(Integer)  # Total blocks allocated for KV cache
    free_blocks = Column(Integer)  # Allocated but not utilized
    atomic_allocations_bytes = Column(Integer)
    fragmentation_ratio = Column(Float)

    # Counts
    num_processes = Column(Integer)
    num_threads = Column(Integer)
    num_blocks = Column(Integer)

    # Derived metrics
    utilized_blocks = Column(Integer, nullable=True)  # Count of blocks that are utilized
    kv_cache_utilization = Column(Float, nullable=True)  # utilized_blocks / allocated_blocks * 100
    vllm_memory_utilization = Column(Float, nullable=True)  # used_bytes / total_bytes * 100
    memory_fragmentation = Column(Float, nullable=True)  # same as fragmentation_ratio

    # JSON data for complex structures
    vllm_metrics = Column(String, nullable=True)

    # Relationships
    node = relationship("Node", back_populates="snapshots")
    processes = relationship("Process", back_populates="snapshot", cascade="all, delete-orphan")
    threads = relationship("Thread", back_populates="snapshot", cascade="all, delete-orphan")
    blocks = relationship("Block", back_populates="snapshot", cascade="all, delete-orphan")
    nsight_metrics = relationship("NsightMetric", back_populates="snapshot", cascade="all, delete-orphan")


class Process(Base):
    """Process using GPU memory"""
    __tablename__ = 'processes'

    id = Column(Integer, primary_key=True, autoincrement=True)
    snapshot_id = Column(Integer, ForeignKey('vram_snapshots.id'), index=True)

    pid = Column(Integer, index=True)
    name = Column(String)
    used_bytes = Column(Integer)
    reserved_bytes = Column(Integer)

    snapshot = relationship("VRAMSnapshot", back_populates="processes")


class Thread(Base):
    """Thread with GPU allocations"""
    __tablename__ = 'threads'

    id = Column(Integer, primary_key=True, autoincrement=True)
    snapshot_id = Column(Integer, ForeignKey('vram_snapshots.id'), index=True)

    thread_id = Column(Integer, index=True)
    allocated_bytes = Column(Integer)
    state = Column(String)

    snapshot = relationship("VRAMSnapshot", back_populates="threads")


class Block(Base):
    """Memory block"""
    __tablename__ = 'blocks'

    id = Column(Integer, primary_key=True, autoincrement=True)
    snapshot_id = Column(Integer, ForeignKey('vram_snapshots.id'), index=True)

    block_id = Column(Integer, index=True)
    size = Column(Integer)
    block_type = Column(String)  # 'type' is reserved keyword
    allocated = Column(Integer)  # SQLite doesn't have Boolean, use 0/1
    utilized = Column(Integer)

    snapshot = relationship("VRAMSnapshot", back_populates="blocks")


class NsightMetric(Base):
    """Nsight profiling metrics per process"""
    __tablename__ = 'nsight_metrics'

    id = Column(Integer, primary_key=True, autoincrement=True)
    snapshot_id = Column(Integer, ForeignKey('vram_snapshots.id'), index=True)

    pid = Column(Integer, index=True)
    available = Column(Integer)  # Boolean as 0/1
    atomic_operations = Column(Integer, nullable=True)
    threads_per_block = Column(Integer, nullable=True)
    blocks_per_sm = Column(Integer, nullable=True)
    shared_memory_usage = Column(Integer, nullable=True)
    occupancy = Column(Float, nullable=True)

    snapshot = relationship("VRAMSnapshot", back_populates="nsight_metrics")


# Database setup
def get_database_url():
    """Get database URL from environment or use default SQLite"""
    db_url = os.getenv('DATABASE_URL')
    if db_url:
        return db_url

    # Default to SQLite in user's home directory
    db_path = os.path.expanduser('~/.blackbox_vram.db')
    return f'sqlite:///{db_path}'


def create_database():
    """Create database and tables"""
    engine = create_engine(get_database_url())
    Base.metadata.create_all(engine)
    return engine


def get_session():
    """Get database session"""
    engine = create_engine(get_database_url())
    Session = sessionmaker(bind=engine)
    return Session()


def init_db():
    """Initialize database (create tables if they don't exist)"""
    engine = create_database()
    print(f"Database initialized at: {get_database_url()}")
    return engine