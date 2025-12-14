package ui

import (
	"context"
	"fmt"
	"strings"
	"time"

	"github.com/maxdcmn/blackbox-cli/internal/client"
	"github.com/maxdcmn/blackbox-cli/internal/config"
	"github.com/maxdcmn/blackbox-cli/internal/model"

	tea "github.com/charmbracelet/bubbletea"
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

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

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

type DataPoint struct {
	Time          time.Time
	UsedBytes     int64
	TotalBytes    int64
	UsedPercent   float64
	ActiveBlocks  int
	FreeBlocks    int
	Fragmentation float64
}

type DashboardModel struct {
	config          *config.Config
	endpoints       []config.Endpoint
	selected        int
	client          *client.Client
	interval        time.Duration
	timeout         time.Duration
	width           int
	height          int
	last            *model.Snapshot
	lastErr         error
	loaded          bool
	history         []DataPoint
	quitting        bool
	creating        bool
	editing         bool
	helpActive      bool
	newName         string
	inputField      int
	newURL          string
	newEp           string
	newTO           string
	editOldName     string
	cursorPos       [4]int
	metricsScroll   int
	endpointsScroll int
	fetchSequence   int
	focusedPanel    int
	maxVRAMSeen     float64
	maxBlocksSeen   float64
	maxFragSeen     float64
}

func NewDashboard(cfg *config.Config, interval, timeout time.Duration) *DashboardModel {
	m := &DashboardModel{
		config:    cfg,
		endpoints: cfg.Endpoints,
		interval:  interval,
		timeout:   timeout,
		history:   make([]DataPoint, 0, maxHistorySize),
	}
	if len(m.endpoints) > 0 {
		m.selectEndpoint(0)
	}
	return m
}

func (m *DashboardModel) selectEndpoint(idx int) {
	if idx < 0 || idx >= len(m.endpoints) {
		return
	}
	m.selected = idx
	ep := m.endpoints[idx]
	timeout, _ := time.ParseDuration(ep.Timeout)
	m.client = client.New(ep.BaseURL, ep.Endpoint, timeout)
	m.loaded = false
	m.last = nil
	m.lastErr = nil
	m.history = make([]DataPoint, 0, maxHistorySize)
	m.metricsScroll = 0
	m.fetchSequence++
}

type tickMsg time.Time
type snapMsg struct {
	s          *model.Snapshot
	err        error
	endpointID int
	fetchSeq   int
}

func (m *DashboardModel) Init() tea.Cmd {
	if m.client == nil {
		return nil
	}
	m.fetchSequence++
	return tea.Batch(fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence), tick(m.interval))
}

func tick(d time.Duration) tea.Cmd {
	return tea.Tick(d, func(t time.Time) tea.Msg { return tickMsg(t) })
}

func fetchSnapshot(c *client.Client, timeout time.Duration, endpointID int, fetchSeq int) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), timeout)
		defer cancel()
		s, err := c.Snapshot(ctx)
		return snapMsg{s: s, err: err, endpointID: endpointID, fetchSeq: fetchSeq}
	}
}

func (m *DashboardModel) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	if m.helpActive {
		if _, ok := msg.(tea.KeyMsg); ok {
			m.helpActive = false
			return m, nil
		}
	}

	if m.creating {
		return m.updateInputMode(msg, true)
	}
	if m.editing {
		return m.updateInputMode(msg, false)
	}

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width, m.height = msg.Width, msg.Height
		return m, nil

	case tickMsg:
		if m.client != nil {
			return m, tea.Batch(fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence), tick(m.interval))
		}
		return m, tick(m.interval)

	case snapMsg:
		if msg.endpointID != m.selected || msg.fetchSeq != m.fetchSequence {
			return m, nil
		}
		m.loaded = true
		m.lastErr = msg.err
		if msg.err == nil && msg.s != nil {
			m.updateHistory(msg.s)
		}
		return m, nil

	case tea.KeyMsg:
		return m.handleKey(msg)
	}

	return m, nil
}

func (m *DashboardModel) updateHistory(s *model.Snapshot) {
	m.last = s
	m.history = append(m.history, DataPoint{
		Time:          time.Now(),
		UsedBytes:     s.UsedBytes,
		TotalBytes:    s.TotalBytes,
		UsedPercent:   s.UsedPercent,
		ActiveBlocks:  s.ActiveBlocks,
		FreeBlocks:    s.FreeBlocks,
		Fragmentation: s.FragmentationRatio,
	})
	if len(m.history) > maxHistorySize {
		m.history = m.history[1:]
	}

	vramPercent := (float64(s.UsedBytes) / float64(s.TotalBytes)) * 100.0
	if vramPercent > m.maxVRAMSeen {
		m.maxVRAMSeen = vramPercent
	}

	totalBlocks := float64(s.ActiveBlocks + s.FreeBlocks)
	if totalBlocks > m.maxBlocksSeen {
		m.maxBlocksSeen = totalBlocks
	}

	fragPercent := s.FragmentationRatio * 100.0
	if fragPercent > m.maxFragSeen {
		m.maxFragSeen = fragPercent
	}
}

func (m *DashboardModel) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	key := msg.String()
	if m.creating || m.editing || m.helpActive {
		return m, nil
	}

	switch key {
	case "q", "ctrl+c":
		m.quitting = true
		return m, tea.Quit
	case "?":
		m.helpActive = !m.helpActive
		return m, nil
	case "tab":
		m.focusedPanel = (m.focusedPanel + 1) % 2
		return m, nil
	case "j", "down":
		return m.handleDown()
	case "k", "up":
		return m.handleUp()
	case "n":
		m.creating = true
		m.newName = ""
		m.newURL = "http://127.0.0.1:8080"
		m.newEp = "/vram"
		m.newTO = "2s"
		m.inputField = 0
		m.cursorPos = [4]int{0, len(m.newURL), len(m.newEp), len(m.newTO)}
		return m, nil
	case "e":
		if len(m.endpoints) > 0 && m.selected < len(m.endpoints) {
			ep := m.endpoints[m.selected]
			m.editing = true
			m.editOldName = ep.Name
			m.newName = ep.Name
			m.newURL = ep.BaseURL
			m.newEp = ep.Endpoint
			m.newTO = ep.Timeout
			m.inputField = 0
			m.cursorPos = [4]int{len(m.newName), len(m.newURL), len(m.newEp), len(m.newTO)}
			return m, nil
		}
	case "d":
		if len(m.endpoints) > 0 && m.selected < len(m.endpoints) {
			name := m.endpoints[m.selected].Name
			if err := config.RemoveEndpoint(m.config, name); err == nil {
				m.endpoints = m.config.Endpoints
				if m.selected >= len(m.endpoints) {
					m.selected = len(m.endpoints) - 1
				}
				if len(m.endpoints) > 0 {
					m.selectEndpoint(m.selected)
					return m, tea.Batch(fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence), tick(m.interval))
				}
				m.client = nil
			}
		}
	case "r":
		if m.client != nil {
			m.loaded = false
			m.lastErr = nil
			m.fetchSequence++
			return m, fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence)
		}
	}
	return m, nil
}

func (m *DashboardModel) handleDown() (tea.Model, tea.Cmd) {
	if m.focusedPanel == 1 {
		if m.last != nil {
			threadCount := min(len(m.last.Threads), maxThreads)
			if len(m.last.Threads) > maxThreads {
				threadCount++
			}
			totalRows := 5 + len(m.last.Processes) + threadCount
			sizes := calculateContainerSizes(m.width, m.height)
			maxVisibleRows := sizes.MetricsGrid.Height - 2
			if totalRows > maxVisibleRows && m.metricsScroll < totalRows-maxVisibleRows {
				m.metricsScroll++
				return m, nil
			}
		}
	} else if m.focusedPanel == 0 && m.selected < len(m.endpoints)-1 {
		m.selectEndpoint(m.selected + 1)
		return m, tea.Batch(fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence), tick(m.interval))
	}
	return m, nil
}

func (m *DashboardModel) handleUp() (tea.Model, tea.Cmd) {
	if m.focusedPanel == 1 {
		if m.metricsScroll > 0 {
			m.metricsScroll--
			return m, nil
		}
	} else if m.focusedPanel == 0 && m.selected > 0 {
		m.selectEndpoint(m.selected - 1)
		return m, tea.Batch(fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence), tick(m.interval))
	}
	return m, nil
}

func (m *DashboardModel) View() string {
	if m.quitting {
		return ""
	}

	if m.creating || m.editing {
		return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, m.renderInputMode(m.creating))
	}

	sizes := calculateContainerSizes(m.width, m.height)
	endpointsPanel := m.renderEndpointsPanel(sizes.Endpoints.Width, sizes.Endpoints.Height, m.focusedPanel == 0)
	metricsGrid := m.renderMetricsGrid(sizes.MetricsGrid.Width, sizes.MetricsGrid.Height, m.focusedPanel == 1)
	dataPanel := m.renderDataPanel(sizes.Data.Width, sizes.Data.Height, false)
	statusBar := m.renderStatusBar(sizes.StatusBar.Width, sizes.StatusBar.Height, m.focusedPanel == 0)

	leftSide := lipgloss.JoinVertical(lipgloss.Left, endpointsPanel, metricsGrid)
	separator := lipgloss.NewStyle().Foreground(lipgloss.Color(colorDim)).Render("│")
	main := lipgloss.JoinHorizontal(lipgloss.Left, leftSide, separator, dataPanel)
	content := lipgloss.JoinVertical(lipgloss.Left, main, statusBar)

	if m.helpActive {
		helpText := `Keyboard Shortcuts
?:        - Show this help
q, ctrl+c - Quit
Tab       - Switch between panels
j, k      - Navigate/scroll in focused panel
n         - Create new endpoint
e         - Edit selected endpoint
d         - Delete selected endpoint
r         - Refresh data
Press any key to close`
		popup := popupStyle.Width(50).Render(helpText)
		popup = lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, popup)
		return lipgloss.JoinVertical(lipgloss.Left, content, popup)
	}

	return content
}

func (m *DashboardModel) renderMetricsGrid(width, height int, focused bool) string {
	width, height = ensureMin(width, height, 20, 5)

	var b strings.Builder
	header := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color(colorText)).Render("Properties") + "\n"
	b.WriteString(header + "\n")

	var rows []string
	labelStyle := lipgloss.NewStyle().Foreground(lipgloss.Color(colorText)).Bold(true)

	if m.last == nil || m.lastErr != nil {
		rows = []string{
			fmt.Sprintf("%s %s", labelStyle.Render("VRAM Usage:"), styleColor(colorMuted).Render("--/-- GB")),
			fmt.Sprintf("%s %s", labelStyle.Render("Memory Blocks:"), styleColor(colorMuted).Render("--/-- (Total: --)")),
			fmt.Sprintf("%s %s", labelStyle.Render("Processes:"), styleColor(colorMuted).Render("-- (none)")),
			fmt.Sprintf("%s %s", labelStyle.Render("Fragmentation:"), styleColor(colorMuted).Render("--%%")),
		}
	} else {
		usedGB := float64(m.last.UsedBytes) / gbDivisor
		totalGB := float64(m.last.TotalBytes) / gbDivisor
		freeGB := float64(m.last.FreeBytes) / gbDivisor
		activeBlocks := m.last.ActiveBlocks
		freeBlocks := m.last.FreeBlocks
		fragmentation := m.last.FragmentationRatio * 100.0

		rows = []string{
			fmt.Sprintf("%s %s/%s GB", labelStyle.Render("VRAM Usage:"),
				styleColor(colorOrange).Render(fmt.Sprintf("%.2f", usedGB)),
				styleColor(colorOrange).Render(fmt.Sprintf("%.2f", totalGB))),
			fmt.Sprintf("%s %s GB", labelStyle.Render("Free VRAM:"),
				styleColor(colorGreen).Render(fmt.Sprintf("%.2f", freeGB))),
			fmt.Sprintf("%s %s/%s %s", labelStyle.Render("Memory Blocks:"),
				styleColor(colorOrange).Render(fmt.Sprintf("%d", activeBlocks)),
				styleColor(colorYellow).Render(fmt.Sprintf("%d", freeBlocks)),
				styleColor(colorItalic).Render(fmt.Sprintf("(Total: %d)", activeBlocks+freeBlocks))),
			fmt.Sprintf("%s %s", labelStyle.Render("Fragmentation:"),
				styleColor(getPercentColor(fragmentation)).Render(fmt.Sprintf("%.2f%%", fragmentation))),
			fmt.Sprintf("%s %s", labelStyle.Render("Processes:"),
				styleColor(colorCyan).Render(fmt.Sprintf("%d", len(m.last.Processes)))),
		}

		for i, proc := range m.last.Processes {
			if i >= 5 {
				break
			}
			procUsedGB := float64(proc.UsedBytes) / gbDivisor
			procName := proc.Name
			if len(procName) > 20 {
				procName = procName[:20] + "..."
			}
			rows = append(rows, fmt.Sprintf("%s %s %s",
				labelStyle.Render(fmt.Sprintf("  PID %d:", proc.PID)),
				styleColor(colorItalic).Render(procName),
				styleColor(colorOrange).Render(fmt.Sprintf("%.2f GB", procUsedGB))))
		}

		if len(m.last.Threads) > 0 {
			threadCount := len(m.last.Threads)
			displayCount := min(threadCount, maxThreads)
			rows = append(rows, fmt.Sprintf("%s %s",
				labelStyle.Render("Threads:"),
				styleColor(colorCyan).Render(fmt.Sprintf("%d", threadCount))))
			for i := 0; i < displayCount; i++ {
				thread := m.last.Threads[i]
				threadGB := float64(thread.AllocatedBytes) / gbDivisor
				rows = append(rows, fmt.Sprintf("%s %s %s",
					labelStyle.Render(fmt.Sprintf("  Thread %d:", thread.ThreadID)),
					styleColor(colorItalic).Render(thread.State),
					styleColor(colorOrange).Render(fmt.Sprintf("%.2f GB", threadGB))))
			}
			if threadCount > maxThreads {
				rows = append(rows, styleColor(colorDim).Render(fmt.Sprintf("  ... and %d more", threadCount-maxThreads)))
			}
		}

		if m.last.VLLMMetrics != "" {
			metrics := parseVLLMMetrics(m.last.VLLMMetrics)
			if metrics.requestsRunning >= 0 {
				rows = append(rows, fmt.Sprintf("%s %s",
					labelStyle.Render("Requests Running:"),
					styleColor(colorCyan).Render(fmt.Sprintf("%.0f", metrics.requestsRunning))))
			}
			if metrics.requestsWaiting >= 0 {
				rows = append(rows, fmt.Sprintf("%s %s",
					labelStyle.Render("Requests Waiting:"),
					styleColor(colorYellow).Render(fmt.Sprintf("%.0f", metrics.requestsWaiting))))
			}
			if metrics.kvCacheUsage >= 0 {
				rows = append(rows, fmt.Sprintf("%s %s",
					labelStyle.Render("KV Cache Usage:"),
					styleColor(colorOrange).Render(fmt.Sprintf("%.1f%%", metrics.kvCacheUsage*100))))
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
	header := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color(colorText)).Render("Endpoints")
	b.WriteString(header + "\n\n")

	innerHeight := max(1, height-3)
	totalEndpoints := len(m.endpoints)

	if m.endpointsScroll < 0 {
		m.endpointsScroll = 0
	}
	if m.selected < m.endpointsScroll {
		m.endpointsScroll = m.selected
	} else if m.selected >= m.endpointsScroll+innerHeight {
		m.endpointsScroll = m.selected - innerHeight + 1
	}
	if m.endpointsScroll > totalEndpoints-innerHeight {
		m.endpointsScroll = max(0, totalEndpoints-innerHeight)
	}

	visibleEndpoints := m.endpoints[m.endpointsScroll:]
	if len(visibleEndpoints) > innerHeight {
		visibleEndpoints = visibleEndpoints[:innerHeight]
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

	usedMB := int(m.last.UsedBytes / (1024 * 1024))
	totalMB := int(m.last.TotalBytes / (1024 * 1024))
	vramMax := maxFloat(100.0, m.maxVRAMSeen)
	vramContent := m.renderMetricContent("VRAM Usage", boxHeight, width, usedMB, totalMB, 0, m.getVRAMHistory(), vramColor, vramMax)

	blocksMax := maxFloat(100.0, m.maxBlocksSeen)
	blocksContent := m.renderMetricContent("Memory Blocks", boxHeight, width, m.last.ActiveBlocks, m.last.FreeBlocks, 0, m.getBlocksHistory(), blocksColor, blocksMax)

	fragMax := maxFloat(100.0, m.maxFragSeen)
	fragmentationContent := m.renderMetricContent("Fragmentation", boxHeight, width, int(m.last.FragmentationRatio*100), 100, 0, m.getFragmentationHistory(), fragmentationColor, fragMax)

	emptyLine := lipgloss.NewStyle().Background(lipgloss.Color(colorBg)).Render(strings.Repeat(" ", max(0, width-2)))
	combined := strings.Join([]string{
		strings.TrimRight(vramContent, "\n"),
		emptyLine,
		strings.TrimRight(blocksContent, "\n"),
		emptyLine,
		strings.TrimRight(fragmentationContent, "\n"),
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

	if len(history) >= 2 {
		chartHeight := max(4, height-1)
		chartOutput := m.renderSparklineChart(history, width-2, chartHeight, color, fixedMax, title)
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

type point struct {
	x, y int
	val  float64
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

func (m *DashboardModel) getHistory(extractor func(DataPoint) float64) []float64 {
	values := make([]float64, len(m.history))
	for i, dp := range m.history {
		values[i] = extractor(dp)
	}
	return values
}

func (m *DashboardModel) getVRAMHistory() []float64 {
	return m.getHistory(func(dp DataPoint) float64 {
		if dp.TotalBytes > 0 {
			return (float64(dp.UsedBytes) / float64(dp.TotalBytes)) * 100.0
		}
		return 0
	})
}

func (m *DashboardModel) getBlocksHistory() []float64 {
	return m.getHistory(func(dp DataPoint) float64 { return float64(dp.ActiveBlocks) })
}

func (m *DashboardModel) getFragmentationHistory() []float64 {
	return m.getHistory(func(dp DataPoint) float64 { return dp.Fragmentation * 100.0 })
}

func (m *DashboardModel) renderStatusBar(width, height int, endpointsFocused bool) string {
	width, height = ensureMin(width, height, 10, 1)

	helpText := styleColor(colorItalic).Render("?: help")
	leftContent := helpText
	if endpointsFocused {
		leftText := styleColor(colorItalic).Render("n: new  e: edit  d: delete  q: quit")
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

func (m *DashboardModel) renderInputMode(isCreate bool) string {
	var b strings.Builder
	if isCreate {
		b.WriteString("Create New Endpoint\n\n")
	} else {
		b.WriteString("Edit Endpoint\n\n")
	}

	fields := []*string{&m.newName, &m.newURL, &m.newEp, &m.newTO}
	labels := []string{"Name: ", "Base URL: ", "Endpoint: ", "Timeout: "}

	for i, field := range fields {
		style := fieldStyle
		if i == m.inputField {
			style = activeFieldStyle
		}
		b.WriteString(style.Render(labels[i]))
		fieldValue := *field
		cursorPos := m.cursorPos[i]
		if i == m.inputField {
			if cursorPos >= 0 && cursorPos <= len(fieldValue) {
				b.WriteString(fieldValue[:cursorPos] + "█" + fieldValue[cursorPos:])
			} else {
				b.WriteString(fieldValue + "█")
			}
		} else {
			b.WriteString(fieldValue)
		}
		b.WriteString("\n")
	}

	b.WriteString("\nTab: next field  Enter: save  Esc: cancel")
	return popupStyle.Width(60).Render(b.String())
}

func (m *DashboardModel) updateInputMode(msg tea.Msg, isCreate bool) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.creating = false
			m.editing = false
			return m, nil
		case "enter":
			if m.newName == "" {
				return m, nil
			}
			ep := config.Endpoint{
				Name:     m.newName,
				BaseURL:  m.newURL,
				Endpoint: m.newEp,
				Timeout:  m.newTO,
			}
			var err error
			if isCreate {
				err = config.AddEndpoint(m.config, ep)
			} else {
				err = config.UpdateEndpoint(m.config, m.editOldName, ep)
			}
			if err == nil {
				m.endpoints = m.config.Endpoints
				if isCreate {
					m.selected = len(m.endpoints) - 1
				} else {
					for i, e := range m.endpoints {
						if e.Name == ep.Name {
							m.selected = i
							break
						}
					}
				}
				m.selectEndpoint(m.selected)
				m.creating = false
				m.editing = false
				return m, tea.Batch(fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence), tick(m.interval))
			}
		case "tab":
			m.ensureCursorInBounds()
			m.inputField = (m.inputField + 1) % 4
			m.ensureCursorInBounds()
			return m, nil
		case "left":
			pos := &m.cursorPos[m.inputField]
			if *pos > 0 {
				*pos--
			}
			return m, nil
		case "right":
			field := m.getFieldValue()
			pos := &m.cursorPos[m.inputField]
			if field != nil && *pos < len(*field) {
				*pos++
			}
			return m, nil
		case "home":
			m.cursorPos[m.inputField] = 0
			return m, nil
		case "end":
			field := m.getFieldValue()
			if field != nil {
				m.cursorPos[m.inputField] = len(*field)
			}
			return m, nil
		case "backspace":
			field := m.getFieldValue()
			pos := &m.cursorPos[m.inputField]
			if field != nil && *pos > 0 {
				*field = (*field)[:*pos-1] + (*field)[*pos:]
				*pos--
			}
			return m, nil
		case "delete":
			field := m.getFieldValue()
			pos := &m.cursorPos[m.inputField]
			if field != nil && *pos < len(*field) {
				*field = (*field)[:*pos] + (*field)[*pos+1:]
			}
			return m, nil
		default:
			if msg.Type == tea.KeyRunes {
				field := m.getFieldValue()
				pos := &m.cursorPos[m.inputField]
				if field != nil {
					*field = (*field)[:*pos] + string(msg.Runes) + (*field)[*pos:]
					*pos += len(msg.Runes)
				}
			}
		}
	}
	return m, nil
}

func (m *DashboardModel) getFieldValue() *string {
	fields := []*string{&m.newName, &m.newURL, &m.newEp, &m.newTO}
	if m.inputField >= 0 && m.inputField < len(fields) {
		return fields[m.inputField]
	}
	return nil
}

func (m *DashboardModel) ensureCursorInBounds() {
	fields := []*string{&m.newName, &m.newURL, &m.newEp, &m.newTO}
	if m.inputField >= 0 && m.inputField < len(fields) {
		fieldLen := len(*fields[m.inputField])
		if m.cursorPos[m.inputField] < 0 {
			m.cursorPos[m.inputField] = 0
		} else if m.cursorPos[m.inputField] > fieldLen {
			m.cursorPos[m.inputField] = fieldLen
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

func styleColor(color string) lipgloss.Style {
	return lipgloss.NewStyle().Foreground(lipgloss.Color(color))
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
