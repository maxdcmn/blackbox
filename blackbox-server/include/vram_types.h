#pragma once

#include <string>
#include <vector>
#include <map>

struct MemoryBlock {
    unsigned long long address;
    unsigned long long size;
    std::string type;
    int block_id;
    bool allocated;
    bool utilized;
    std::string model_id;  // Model identifier (e.g., "TinyLlama")
    int port;              // Port the model is running on
};

struct ProcessMemory {
    unsigned int pid;
    std::string name;
    unsigned long long used_bytes;
    unsigned long long reserved_bytes;
};

struct ThreadInfo {
    int thread_id;
    unsigned long long allocated_bytes;
    std::string state;
};

struct NsightMetrics {
    unsigned long long atomic_operations;
    unsigned long long threads_per_block;
    double occupancy;
    unsigned long long active_blocks;
    unsigned long long memory_throughput;
    unsigned long long dram_read_bytes;
    unsigned long long dram_write_bytes;
    bool available;
};

struct ModelVRAMInfo {
    std::string model_id;
    int port;
    unsigned long long allocated_vram_bytes;  // VRAM allocated for this model
    unsigned long long used_kv_cache_bytes;   // Actual used KV cache bytes for this model
};

struct DetailedVRAMInfo {
    unsigned long long total;
    unsigned long long used;
    unsigned long long free;
    unsigned long long reserved;
    std::vector<MemoryBlock> blocks;
    std::vector<ProcessMemory> processes;
    std::vector<ThreadInfo> threads;
    unsigned int allocated_blocks;
    unsigned int utilized_blocks;
    unsigned int free_blocks;
    unsigned long long atomic_allocations;
    double fragmentation_ratio;
    std::map<unsigned int, NsightMetrics> nsight_metrics;
    unsigned long long used_kv_cache_bytes;  // Total actual used KV cache bytes (sum across all models)
    double prefix_cache_hit_rate;            // Prefix cache hit rate (0.0-100.0)
    std::vector<ModelVRAMInfo> models;        // Per-model breakdown
};

struct VLLMBlockData {
    unsigned int num_gpu_blocks;
    unsigned long long block_size;
    double kv_cache_usage_perc;
    double prefix_cache_hit_rate;
    bool available;
};

struct AggregatedStats {
    double min;
    double max;
    double avg;
    double p95;
    double p99;
    unsigned int count;
};

struct AggregatedVRAMInfo {
    unsigned long long total_vram_bytes;
    AggregatedStats allocated_vram_bytes;
    AggregatedStats used_kv_cache_bytes;
    AggregatedStats prefix_cache_hit_rate;
    AggregatedStats num_requests_running;
    AggregatedStats num_requests_waiting;
    std::vector<ModelVRAMInfo> models;
    unsigned long long window_seconds;
    unsigned int sample_count;
};

