package ui

import (
	"fmt"
	"time"
)

func formatDuration(seconds int64) string {
	hrs := seconds / 3600
	seconds %= 3600
	mins := seconds / 60
	secs := seconds % 60
	return fmt.Sprintf("%02d:%02d:%02d", hrs, mins, secs)
}

func formatTime(t time.Time) string {
	return t.Format("15:04:05")
}

func truncateString(s string, maxLen int) string {
	if maxLen <= 3 {
		if len(s) > maxLen {
			return s[:maxLen]
		}
		return s
	}
	if len(s) <= maxLen {
		return s
	}
	return s[:maxLen-3] + "..."
}

func findMax(values []float64) float64 {
	if len(values) == 0 {
		return 1.0
	}
	max := values[0]
	for _, v := range values {
		if v > max {
			max = v
		}
	}
	if max == 0 {
		return 1.0
	}
	return max
}

func normalizeValue(value, min, max float64) float64 {
	if max == min {
		return 0
	}
	normalized := (value - min) / (max - min)
	if normalized < 0 {
		return 0
	}
	if normalized > 1 {
		return 1
	}
	return normalized
}
