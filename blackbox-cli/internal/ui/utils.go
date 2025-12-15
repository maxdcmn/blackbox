package ui

import (
	"github.com/charmbracelet/lipgloss"
)

const (
	maxHistorySize = 50
	maxThreads     = 10
	version        = "0.1.0"
	gbDivisor      = 1024 * 1024 * 1024
	colorFocused   = "46"
	colorUnfocused = "15"
	colorText      = "15"
	colorMuted     = "250"
	colorDim       = "240"
	colorItalic    = "245"
	colorBg        = "0"
	colorOrange    = "214"
	colorYellow    = "220"
	colorCyan      = "39"
	colorGreen     = "46"
	colorRed       = "196"
)

func maxFloat(a, b float64) float64 {
	if a > b {
		return a
	}
	return b
}

func abs(x int) int {
	if x < 0 {
		return -x
	}
	return x
}

func ensureMin(w, h, minW, minH int) (int, int) {
	if w < minW {
		w = minW
	}
	if h < minH {
		h = minH
	}
	return w, h
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

func styleColor(color string) lipgloss.Style {
	return lipgloss.NewStyle().Foreground(lipgloss.Color(color))
}

func getPercentColor(percent float64) string {
	if percent >= 90 {
		return colorRed
	} else if percent >= 70 {
		return colorOrange
	} else if percent >= 50 {
		return colorYellow
	}
	return colorGreen
}

func borderStyle(width, height int, focused bool) lipgloss.Style {
	borderColor := colorFocused
	if !focused {
		borderColor = colorUnfocused
	}
	return lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color(borderColor)).
		Padding(0, 1).
		Background(lipgloss.Color(colorBg)).
		Width(width).
		Height(height)
}

var (
	statusBarStyle = lipgloss.NewStyle().
			Height(1).
			Foreground(lipgloss.Color(colorMuted)).
			Background(lipgloss.Color(colorBg)).
			Padding(0, 1).
			Bold(false)

	popupStyle = lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(lipgloss.Color(colorUnfocused)).
			Padding(1, 2).
			Background(lipgloss.Color(colorBg)).
			Foreground(lipgloss.Color(colorMuted))

	fieldStyle = lipgloss.NewStyle().
			Foreground(lipgloss.Color(colorItalic))

	activeFieldStyle = lipgloss.NewStyle().
				Background(lipgloss.Color(colorText)).
				Foreground(lipgloss.Color(colorBg)).
				Bold(true)

	vramColor          = lipgloss.Color("28")
	blocksColor        = lipgloss.Color("34")
	fragmentationColor = lipgloss.Color("40")
)
