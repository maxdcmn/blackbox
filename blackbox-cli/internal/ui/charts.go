package ui

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/lipgloss"
)

type point struct {
	x, y int
	val  float64
}

func (m *DashboardModel) renderSparklineChart(values []float64, width, height int, color lipgloss.Color, fixedMax float64, title string) string {
	if len(values) < 2 {
		return ""
	}
	width, height = ensureMin(width, height, 20, 6)

	maxVal := fixedMax
	if fixedMax <= 0 {
		maxVal = findMax(values)
		if maxVal == 0 {
			maxVal = 1
		}
	}

	minVal := values[0]
	for _, v := range values {
		if v < minVal {
			minVal = v
		}
	}

	if fixedMax > 0 {
		minVal = 0
	} else if minVal < 0 {
		minVal = 0
	}

	if maxVal <= minVal {
		maxVal = minVal + 1
	}

	chartWidth := max(10, width)
	chartHeight := max(4, height)
	gridHeight := max(3, chartHeight-1)

	displayCount := min(len(values), chartWidth-2)
	if displayCount < 2 {
		displayCount = min(len(values), 2)
	}
	displayValues := values[len(values)-displayCount:]

	grid := make([][]rune, gridHeight)
	for i := range grid {
		grid[i] = make([]rune, chartWidth)
		for j := range grid[i] {
			if (i+j)%4 == 0 {
				grid[i][j] = '·'
			} else {
				grid[i][j] = ' '
			}
		}
	}

	for i := 0; i < gridHeight; i++ {
		grid[i][0] = '│'
	}
	for j := 0; j < chartWidth; j++ {
		grid[gridHeight-1][j] = '─'
	}
	grid[gridHeight-1][0] = '└'

	if len(displayValues) > 1 {
		points := m.calculateChartPoints(displayValues, chartWidth, gridHeight, minVal, maxVal)
		m.drawChartArea(grid, points, chartWidth, gridHeight)
		m.drawChartLine(grid, points)
		m.highlightCurrentPoint(grid, points, chartWidth, gridHeight)
	}

	var b strings.Builder
	colorStyle := lipgloss.NewStyle().Foreground(color)

	if chartHeight > 0 {
		b.WriteString(strings.Repeat(" ", chartWidth) + "\n")
	}

	for i := 0; i < gridHeight && i < len(grid); i++ {
		b.WriteString(colorStyle.Render(string(grid[i])) + "\n")
	}

	return b.String()
}

func (m *DashboardModel) calculateChartPoints(values []float64, width, height int, minVal, maxVal float64) []point {
	points := make([]point, len(values))
	for i, val := range values {
		normalized := normalizeValue(val, minVal, maxVal)
		x := 1 + (i * (width - 2) / max(1, len(values)-1))
		if x >= width {
			x = width - 1
		}
		y := height - 2 - int(normalized*float64(height-2))
		if y < 0 {
			y = 0
		}
		if y >= height-1 {
			y = height - 2
		}
		points[i] = point{x: x, y: y, val: val}
	}
	return points
}

func (m *DashboardModel) drawChartArea(grid [][]rune, points []point, width, height int) {
	for i := 0; i < len(points)-1; i++ {
		p1, p2 := points[i], points[i+1]
		bottomY := height - 2
		topY := min(p1.y, p2.y)
		for y := bottomY; y >= topY; y-- {
			for x := p1.x; x <= p2.x && x < width; x++ {
				if x > 0 && y >= 0 && y < height-1 {
					distFromTop := bottomY - y
					if distFromTop == 0 || y == topY {
						grid[y][x] = '▁'
					} else if distFromTop <= 2 {
						grid[y][x] = '▂'
					} else {
						grid[y][x] = '▃'
					}
				}
			}
		}
	}
}

func (m *DashboardModel) drawChartLine(grid [][]rune, points []point) {
	for i := 0; i < len(points)-1; i++ {
		p1, p2 := points[i], points[i+1]
		drawLine(grid, p1.x, p1.y, p2.x, p2.y, '●', '━', '┃', '╱', '╲')
	}
}

func (m *DashboardModel) highlightCurrentPoint(grid [][]rune, points []point, width, height int) {
	if len(points) > 0 {
		last := points[len(points)-1]
		if last.x > 0 && last.x < width && last.y >= 0 && last.y < height-1 {
			grid[last.y][last.x] = '●'
		}
	}
}

func drawLine(grid [][]rune, x1, y1, x2, y2 int, pointChar, hChar, vChar, upChar, downChar rune) {
	dx := abs(x2 - x1)
	dy := abs(y2 - y1)

	if dx == 0 && dy == 0 {
		if x1 > 0 && x1 < len(grid[0]) && y1 >= 0 && y1 < len(grid) {
			grid[y1][x1] = pointChar
		}
		return
	}

	steps := max(dx, dy)
	if steps == 0 {
		steps = 1
	}

	for i := 0; i <= steps; i++ {
		t := float64(i) / float64(steps)
		x := x1 + int(float64(x2-x1)*t)
		y := y1 + int(float64(y2-y1)*t)

		if x > 0 && x < len(grid[0]) && y >= 0 && y < len(grid)-1 {
			var char rune
			if i == 0 || i == steps {
				char = pointChar
			} else if dx > dy {
				char = hChar
			} else if dy > dx {
				char = vChar
			} else if (x2 > x1 && y2 < y1) || (x2 < x1 && y2 > y1) {
				char = upChar
			} else {
				char = downChar
			}
			grid[y][x] = char
		}
	}
}

func (m *DashboardModel) formatMetricValues(title string, val1, val2, val3 int) string {
	switch title {
	case "VRAM Usage":
		percent := 0.0
		if val2 > 0 {
			percent = (float64(val1) / float64(val2)) * 100.0
		}
		return fmt.Sprintf("%s %s",
			styleColor(getPercentColor(percent)).Render(fmt.Sprintf("%d/%d MB", val1, val2)),
			styleColor(getPercentColor(percent)).Render(fmt.Sprintf("(%.1f%%)", percent)))
	case "Memory Blocks":
		total := val1 + val2
		return fmt.Sprintf("%s  %s  %s",
			styleColor(colorOrange).Render(fmt.Sprintf("Utilized: %d", val1)),
			styleColor(colorGreen).Render(fmt.Sprintf("Free: %d", val2)),
			styleColor(colorItalic).Render(fmt.Sprintf("Total: %d", total)))
	case "Fragmentation":
		return styleColor(getPercentColor(float64(val1))).Render(fmt.Sprintf("%.2f%%", float64(val1)))
	default:
		percent := 0.0
		if val2 > 0 {
			percent = (float64(val1) / float64(val2)) * 100.0
		}
		return fmt.Sprintf("%s %s",
			styleColor(colorRed).Render(fmt.Sprintf("%d/%d", val1, val2)),
			styleColor(colorRed).Render(fmt.Sprintf("(%.1f%%)", percent)))
	}
}
