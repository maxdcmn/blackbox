package model

// VRAM snapshot from blackbox-server /vram endpoint
type Snapshot struct {
	TotalVRAMBytes      int64        `json:"total_vram_bytes"`      // Total VRAM available on the GPU
	AllocatedVRAMBytes  int64        `json:"allocated_vram_bytes"`  // What CUDA/NVML says is allocated (vLLM preallocating)
	UsedKVCacheBytes    int64        `json:"used_kv_cache_bytes"`   // Actual used KV cache (num_blocks * block_size * kv_cache_usage_perc)
	PrefixCacheHitRate  float64      `json:"prefix_cache_hit_rate"` // Prefix cache hit rate (0.0-100.0)
	Models              []ModelInfo  `json:"models"`                 // Per-model breakdown
}

type ModelInfo struct {
	ModelID            string `json:"model_id"`
	Port               int    `json:"port"`
	AllocatedVRAMBytes int64  `json:"allocated_vram_bytes"`
	UsedKVCacheBytes   int64  `json:"used_kv_cache_bytes"`
}

// AggregatedStats represents statistical aggregation over a time window
type AggregatedStats struct {
	Min   float64 `json:"min"`
	Max   float64 `json:"max"`
	Avg   float64 `json:"avg"`
	P95   float64 `json:"p95"`
	P99   float64 `json:"p99"`
	Count int     `json:"count"`
}

// AggregatedSnapshot from blackbox-server /vram/aggregated endpoint
type AggregatedSnapshot struct {
	TotalVRAMBytes      int64                    `json:"total_vram_bytes"`
	WindowSeconds       int                      `json:"window_seconds"`
	SampleCount         int                      `json:"sample_count"`
	AllocatedVRAMBytes  AggregatedStats          `json:"allocated_vram_bytes"`
	UsedKVCacheBytes    AggregatedStats          `json:"used_kv_cache_bytes"`
	PrefixCacheHitRate  AggregatedStats          `json:"prefix_cache_hit_rate"`
	NumRequestsRunning  AggregatedStats          `json:"num_requests_running"`
	NumRequestsWaiting  AggregatedStats          `json:"num_requests_waiting"`
	Models              []ModelInfo              `json:"models"`
}
