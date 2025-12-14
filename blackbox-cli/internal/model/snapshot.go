package model

// VRAM snapshot from blackbox-server /vram endpoint
type Snapshot struct {
	TotalBytes             int64     `json:"total_bytes"`
	UsedBytes              int64     `json:"used_bytes"`
	FreeBytes              int64     `json:"free_bytes"`
	ReservedBytes          int64     `json:"reserved_bytes"`
	UsedPercent            float64   `json:"used_percent"`
	TotalBlocks            int       `json:"total_blocks"`
	AllocatedBlocks        int       `json:"allocated_blocks"`
	ActiveBlocks           int       `json:"active_blocks"`
	FreeBlocks             int       `json:"free_blocks"`
	AtomicAllocationsBytes int64     `json:"atomic_allocations_bytes"`
	FragmentationRatio     float64   `json:"fragmentation_ratio"`
	Processes              []Process `json:"processes"`
	Threads                []Thread  `json:"threads"`
	Blocks                 []Block   `json:"blocks"`
	VLLMMetrics            string    `json:"vllm_metrics,omitempty"`
}

type Process struct {
	PID           int    `json:"pid"`
	Name          string `json:"name"`
	UsedBytes     int64  `json:"used_bytes"`
	ReservedBytes int64  `json:"reserved_bytes"`
}

type Thread struct {
	ThreadID       int    `json:"thread_id"`
	AllocatedBytes int64  `json:"allocated_bytes"`
	State          string `json:"state"`
}

type Block struct {
	BlockID   int    `json:"block_id"`
	Address   int64  `json:"address"`
	Size      int64  `json:"size"`
	Type      string `json:"type"`
	Allocated bool   `json:"allocated"`
	Utilized  bool   `json:"utilized"`
}
