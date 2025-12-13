package model

// temp snapshot
type Snapshot struct {
	Version string `json:"version,omitempty"`
	TS      int64  `json:"ts"`

	Requests Requests `json:"requests"`
	Tokens   Tokens   `json:"tokens"`
	KV       KV       `json:"kv"`
}

type Requests struct {
	QueueLen int `json:"queue_len"`
	P50ms    int `json:"p50_ms"`
	P95ms    int `json:"p95_ms"`
}

type Tokens struct {
	TPS      float64          `json:"tps"`
	Total    int64            `json:"total"`
	ByIntent map[string]int64 `json:"by_intent,omitempty"`
}

type KV struct {
	UsedMB       int `json:"used_mb"`
	CapacityMB   int `json:"capacity_mb"`
	CompressedMB int `json:"compressed_mb,omitempty"`
}
