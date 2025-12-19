package ui

import (
	"fmt"
	"strings"

	"github.com/charmbracelet/lipgloss"
	"github.com/maxdcmn/blackbox-cli/internal/config"
)

func (m *DashboardModel) renderMetricsGrid(width, height int, focused bool) string {
	width, height = ensureMin(width, height, 20, 5)

	var b strings.Builder
	headerColor := colorText
	if focused {
		headerColor = colorGreen
	}
	header := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color(headerColor)).Render("Properties") + "\n"
	b.WriteString(header + "\n")

	var rows []string
	labelStyle := lipgloss.NewStyle().Foreground(lipgloss.Color(colorText)).Bold(true)

	if m.last == nil || m.lastErr != nil {
		rows = []string{
			fmt.Sprintf("%s %s", labelStyle.Render("Allocated VRAM:"), styleColor(colorMuted).Render("-- GB")),
			fmt.Sprintf("%s %s", labelStyle.Render("Used KV Cache:"), styleColor(colorMuted).Render("-- GB")),
		}
	} else {
		totalGB := float64(m.last.TotalVRAMBytes) / gbDivisor
		allocatedGB := float64(m.last.AllocatedVRAMBytes) / gbDivisor
		usedKVCacheGB := float64(m.last.UsedKVCacheBytes) / gbDivisor
		allocatedPercent := 0.0
		if m.last.TotalVRAMBytes > 0 {
			allocatedPercent = (float64(m.last.AllocatedVRAMBytes) / float64(m.last.TotalVRAMBytes)) * 100.0
		}

		rows = []string{
			fmt.Sprintf("%s %s / %s GB", labelStyle.Render("Allocated VRAM:"),
				styleColor(colorOrange).Render(fmt.Sprintf("%.2f", allocatedGB)),
				styleColor(colorItalic).Render(fmt.Sprintf("%.2f", totalGB))),
			fmt.Sprintf("%s %s", labelStyle.Render("Allocated %:"),
				styleColor(getPercentColor(allocatedPercent)).Render(fmt.Sprintf("%.1f%%", allocatedPercent))),
			fmt.Sprintf("%s %s GB", labelStyle.Render("Used KV Cache:"),
				styleColor(colorGreen).Render(fmt.Sprintf("%.2f", usedKVCacheGB))),
		}

		// Show per-model breakdown
		if len(m.last.Models) > 0 {
			rows = append(rows, "")
			rows = append(rows, labelStyle.Render("Models:"))
			for _, model := range m.last.Models {
				modelAllocatedGB := float64(model.AllocatedVRAMBytes) / gbDivisor
				modelUsedKVCacheGB := float64(model.UsedKVCacheBytes) / gbDivisor
				modelName := model.ModelID
				if len(modelName) > 20 {
					modelName = modelName[:20] + "..."
				}
				rows = append(rows, fmt.Sprintf("%s %s",
					labelStyle.Render("  "+modelName+":"),
					styleColor(colorItalic).Render(fmt.Sprintf("(port %d)", model.Port))))
				rows = append(rows, fmt.Sprintf("%s %s",
					labelStyle.Render("    Used KV Cache:"),
					styleColor(colorGreen).Render(fmt.Sprintf("%.2f GB", modelUsedKVCacheGB))))
				rows = append(rows, fmt.Sprintf("%s %s",
					labelStyle.Render("    Allocated VRAM:"),
					styleColor(colorOrange).Render(fmt.Sprintf("%.2f GB", modelAllocatedGB))))
			}
		}
	}

	headerLines := 2
	innerHeight := height - 2
	maxVisibleRows := max(1, innerHeight-headerLines)
	totalRows := len(rows)
	if m.metricsScroll < 0 {
		m.metricsScroll = 0
	}
	if m.metricsScroll > totalRows-maxVisibleRows {
		m.metricsScroll = max(0, totalRows-maxVisibleRows)
	}

	visibleRows := rows[m.metricsScroll:]
	if len(visibleRows) > maxVisibleRows {
		visibleRows = visibleRows[:maxVisibleRows]
	}

	rowStyle := lipgloss.NewStyle().Width(width - 4).Align(lipgloss.Left)
	for _, row := range visibleRows {
		b.WriteString(rowStyle.Render(row) + "\n")
	}

	currentContent := b.String()
	lines := strings.Split(strings.TrimRight(currentContent, "\n"), "\n")
	fillWidth := max(0, width-4)
	bgFill := lipgloss.NewStyle().Background(lipgloss.Color(colorBg)).Render(strings.Repeat(" ", fillWidth))
	for i := len(lines); i < innerHeight; i++ {
		b.WriteString(bgFill + "\n")
	}

	borderColor := colorFocused
	if !focused {
		borderColor = colorUnfocused
	}

	style := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color(borderColor)).
		Padding(0, 1).
		Background(lipgloss.Color(colorBg)).
		Width(width).
		Height(height)

	return style.Render(b.String())
}

func (m *DashboardModel) renderEndpointsPanel(width, height int, focused bool) string {
	width, height = ensureMin(width, height, 10, 3)

	var b strings.Builder
	headerColor := colorText
	if focused {
		headerColor = colorGreen
	}
	header := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color(headerColor)).Render("Endpoints")
	b.WriteString(header + "\n\n")

	innerHeight := max(1, height-3)
	totalEndpoints := len(m.endpoints)

	if m.endpointsScroll < 0 {
		m.endpointsScroll = 0
	}
	if totalEndpoints == 0 {
		// No endpoints, show empty state
		m.endpointsScroll = 0
	} else {
		if m.selected < 0 {
			m.selected = 0
		}
		if m.selected >= totalEndpoints {
			m.selected = totalEndpoints - 1
		}
		if m.selected < m.endpointsScroll {
			m.endpointsScroll = m.selected
		} else if m.selected >= m.endpointsScroll+innerHeight {
			m.endpointsScroll = m.selected - innerHeight + 1
		}
		if m.endpointsScroll > totalEndpoints-innerHeight {
			m.endpointsScroll = max(0, totalEndpoints-innerHeight)
		}
	}

	var visibleEndpoints []config.Endpoint
	if totalEndpoints > 0 && m.endpointsScroll < totalEndpoints {
		visibleEndpoints = m.endpoints[m.endpointsScroll:]
		if len(visibleEndpoints) > innerHeight {
			visibleEndpoints = visibleEndpoints[:innerHeight]
		}
	}

	availableWidth := max(0, width-4)
	for i, ep := range visibleEndpoints {
		actualIndex := m.endpointsScroll + i
		name := truncateString(ep.Name, max(1, availableWidth))

		if actualIndex == m.selected {
			style := lipgloss.NewStyle().
				Background(lipgloss.Color(colorText)).
				Foreground(lipgloss.Color(colorBg)).
				Bold(true).
				Width(availableWidth).
				Align(lipgloss.Left)
			b.WriteString(style.Render(name) + "\n")
		} else {
			style := lipgloss.NewStyle().
				Background(lipgloss.Color(colorBg)).
				Foreground(lipgloss.Color(colorText)).
				Width(availableWidth).
				Align(lipgloss.Left)
			b.WriteString(style.Render(name) + "\n")
		}
	}

	m.fillToHeight(&b, b.String(), width, height, colorBg)
	return borderStyle(width, height, focused).Render(b.String())
}

func (m *DashboardModel) renderDataPanel(width, height int, focused bool) string {
	borderColor := colorFocused
	if !focused {
		borderColor = colorUnfocused
	}

	if m.client == nil {
		return m.renderEmptyState(width, height, "No endpoint selected\n\nPress 'n' to create one", borderColor)
	}

	if !m.loaded {
		return m.renderEmptyState(width, height, "Loading...", borderColor)
	}

	if m.lastErr != nil && m.last == nil {
		return m.renderEmptyState(width, height, fmt.Sprintf("Error: %s\n\nPress 'r' to retry", m.lastErr.Error()), borderColor)
	}

	innerHeight := height - 2
	availableHeight := innerHeight - 2
	boxHeight := max(5, availableHeight/3)

	allocatedMB := int(m.last.AllocatedVRAMBytes / (1024 * 1024))
	totalMB := int(m.last.TotalVRAMBytes / (1024 * 1024))
	vramMax := maxFloat(100.0, m.maxVRAMSeen)
	vramContent := m.renderMetricContent("Allocated VRAM", boxHeight, width, allocatedMB, totalMB, 0, m.getVRAMHistory(), vramColor, vramMax)

	usedKVCacheMB := int(m.last.UsedKVCacheBytes / (1024 * 1024))
	kvCacheMax := maxFloat(100.0, m.maxBlocksSeen)
	kvCacheContent := m.renderMetricContent("Used KV Cache", boxHeight, width, usedKVCacheMB, 0, 0, m.getBlocksHistory(), blocksColor, kvCacheMax)

	prefixHitRate := int(m.last.PrefixCacheHitRate)
	prefixHitRateMax := maxFloat(100.0, m.maxPrefixHitRateSeen)
	prefixHitRateContent := m.renderMetricContent("Prefix Cache Hit Rate", boxHeight, width, prefixHitRate, 0, 0, m.getPrefixCacheHitRateHistory(), prefixHitRateColor, prefixHitRateMax)

	emptyLine := lipgloss.NewStyle().Background(lipgloss.Color(colorBg)).Render(strings.Repeat(" ", max(0, width-2)))
	combined := strings.Join([]string{
		strings.TrimRight(vramContent, "\n"),
		emptyLine,
		strings.TrimRight(kvCacheContent, "\n"),
		emptyLine,
		strings.TrimRight(prefixHitRateContent, "\n"),
	}, "\n")
	return borderStyle(width, height, focused).Render(combined)
}

func (m *DashboardModel) renderEmptyState(width, height int, message string, borderColor string) string {
	width, height = ensureMin(width, height, 10, 3)

	var b strings.Builder
	if m.lastErr != nil || message == "Loading..." {
		b.WriteString("\n")
	}
	emptyStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color(colorDim)).
		Italic(true).
		Align(lipgloss.Center).
		Width(width - 4)
	b.WriteString(emptyStyle.Render(message))
	m.fillToHeight(&b, b.String(), width, height-2, colorBg)
	style := borderStyle(width, height, false)
	style = style.BorderForeground(lipgloss.Color(borderColor))
	return style.Render(b.String())
}

func (m *DashboardModel) renderMetricContent(title string, height, width int, val1, val2, val3 int, history []float64, color lipgloss.Color, fixedMax float64) string {
	width, height = ensureMin(width, height, 10, 5)

	var b strings.Builder
	titleStyle := lipgloss.NewStyle().Foreground(color).Bold(true)
	valuesText := m.formatMetricValues(title, val1, val2, val3)
	b.WriteString(fmt.Sprintf("%s  %s\n", titleStyle.Render(title), valuesText))

	if len(history) >= 1 {
		chartHeight := max(4, height-1)
		historyForChart := history
		if len(history) == 1 {
			historyForChart = []float64{history[0], history[0]}
		}
		chartOutput := m.renderSparklineChart(historyForChart, width-2, chartHeight, color, fixedMax, title)
		b.WriteString(chartOutput)
	} else {
		loadingStyle := lipgloss.NewStyle().Foreground(lipgloss.Color(colorDim)).Italic(true)
		b.WriteString(loadingStyle.Render("Collecting data...") + "\n")
	}

	content := b.String()
	lines := strings.Split(strings.TrimRight(content, "\n"), "\n")
	fillWidth := max(0, width-2)
	bgFill := lipgloss.NewStyle().Background(lipgloss.Color(colorBg)).Render(strings.Repeat(" ", fillWidth))
	for i := len(lines); i < height; i++ {
		b.WriteString(bgFill)
		if i < height-1 {
			b.WriteString("\n")
		}
	}

	return b.String()
}

func (m *DashboardModel) renderStatusBar(width, height int, endpointsFocused bool) string {
	width, height = ensureMin(width, height, 10, 1)

	helpText := styleColor(colorItalic).Render("?: help")
	leftContent := helpText
	if endpointsFocused {
		leftText := styleColor(colorItalic).Render("n: new  e: edit  d: delete  D: deploy  q: quit")
		leftContent = helpText + "  " + leftText
	}

	star := styleColor(colorYellow).Render("★")
	githubURL := "https://github.com/maxdcmn/blackbox"
	githubTextStyled := styleColor(colorYellow).Underline(true).Bold(true).Render("GitHub")
	githubTextLinked := fmt.Sprintf("\x1b]8;;%s\x1b\\%s\x1b]8;;\x1b\\", githubURL, githubTextStyled)
	versionV := styleColor(colorGreen).Bold(true).Render("v")
	versionNum := styleColor(colorGreen).Render(version)
	rightContent := star + " " + githubTextLinked + "  " + versionV + versionNum

	availableWidth := width - 2
	leftLen := lipgloss.Width(leftContent)
	rightLen := lipgloss.Width(rightContent)
	spacerLen := max(1, availableWidth-leftLen-rightLen)

	content := leftContent + strings.Repeat(" ", spacerLen) + rightContent
	return statusBarStyle.Width(width).Height(height).Render(content)
}

func (m *DashboardModel) fillToHeight(b *strings.Builder, content string, width, targetHeight int, bgColor string) {
	lines := strings.Split(content, "\n")
	fillWidth := max(0, width-4)
	bgFill := lipgloss.NewStyle().Background(lipgloss.Color(bgColor)).Render(strings.Repeat(" ", fillWidth))
	for i := len(lines); i < targetHeight; i++ {
		b.WriteString(bgFill + "\n")
	}
}

type vllmMetrics struct {
	requestsRunning float64
	requestsWaiting float64
	kvCacheUsage    float64
}

func parseVLLMMetrics(metricsStr string) vllmMetrics {
	result := vllmMetrics{-1, -1, -1}
	lines := strings.Split(metricsStr, "\n")
	for _, line := range lines {
		line = strings.TrimSpace(line)
		if strings.HasPrefix(line, "#") || line == "" {
			continue
		}
		if idx := strings.LastIndex(line, " "); idx >= 0 {
			var val float64
			if _, err := fmt.Sscanf(line[idx+1:], "%f", &val); err == nil {
				if strings.HasPrefix(line, "vllm:num_requests_running") {
					result.requestsRunning = val
				} else if strings.HasPrefix(line, "vllm:num_requests_waiting") {
					result.requestsWaiting = val
				} else if strings.HasPrefix(line, "vllm:kv_cache_usage_perc") {
					result.kvCacheUsage = val
				}
			}
		}
	}
	return result
}
