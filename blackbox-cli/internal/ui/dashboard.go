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
	version        = "0.1.0"
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

func abs(x int) int {
	if x < 0 {
		return -x
	}
	return x
}

func ensureMinDimensions(width, height, minW, minH int) (int, int) {
	if width < minW {
		width = minW
	}
	if height < minH {
		height = minH
	}
	return width, height
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
	maxTPS          float64
	metricsScroll   int
	endpointsScroll int
}

func NewDashboard(cfg *config.Config, interval, timeout time.Duration) *DashboardModel {
	m := &DashboardModel{
		config:    cfg,
		endpoints: cfg.Endpoints,
		interval:  interval,
		timeout:   timeout,
		history:   make([]DataPoint, 0, maxHistorySize),
		maxTPS:    100.0,
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
}

type tickMsg time.Time
type snapMsg struct {
	s   *model.Snapshot
	err error
}

func (m *DashboardModel) Init() tea.Cmd {
	if m.client == nil {
		return nil
	}
	return tea.Batch(fetchSnapshot(m.client, m.timeout), tick(m.interval))
}

func tick(d time.Duration) tea.Cmd {
	return tea.Tick(d, func(t time.Time) tea.Msg { return tickMsg(t) })
}

func fetchSnapshot(c *client.Client, timeout time.Duration) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), timeout)
		defer cancel()
		s, err := c.Snapshot(ctx)
		return snapMsg{s: s, err: err}
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
			return m, tea.Batch(fetchSnapshot(m.client, m.timeout), tick(m.interval))
		}
		return m, tick(m.interval)

	case snapMsg:
		m.loaded = true
		m.lastErr = msg.err
		if msg.err == nil && msg.s != nil {
			m.last = msg.s
			m.history = append(m.history, DataPoint{
				Time:          time.Now(),
				UsedBytes:     msg.s.UsedBytes,
				TotalBytes:    msg.s.TotalBytes,
				UsedPercent:   msg.s.UsedPercent,
				ActiveBlocks:  msg.s.ActiveBlocks,
				FreeBlocks:    msg.s.FreeBlocks,
				Fragmentation: msg.s.FragmentationRatio,
			})
			if len(m.history) > maxHistorySize {
				m.history = m.history[1:]
			}
		}
		return m, nil

	case tea.KeyMsg:
		switch msg.String() {
		case "q", "ctrl+c":
			m.quitting = true
			return m, tea.Quit
		case "?":
			m.helpActive = !m.helpActive
			return m, nil
		case "j", "down":
			if !m.creating && !m.editing && !m.helpActive {
				if m.last != nil {
					totalRows := 5 + len(m.last.Processes) + len(m.last.Threads)
					sizes := calculateContainerSizes(m.width, m.height)
					maxVisibleRows := sizes.MetricsGrid.Height - 2
					if totalRows > maxVisibleRows && m.metricsScroll < totalRows-maxVisibleRows {
						m.metricsScroll++
						return m, nil
					}
				}
				if m.selected < len(m.endpoints)-1 {
					m.selectEndpoint(m.selected + 1)
					return m, m.Init()
				}
			}
		case "k", "up":
			if !m.creating && !m.editing && !m.helpActive {
				if m.metricsScroll > 0 {
					m.metricsScroll--
					return m, nil
				}
				if m.selected > 0 {
					m.selectEndpoint(m.selected - 1)
					return m, m.Init()
				}
			}
		case "n":
			m.creating = true
			m.newName = ""
			m.newURL = "http://127.0.0.1:8080"
			m.newEp = "/vram"
			m.newTO = "2s"
			m.inputField = 0
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
						return m, m.Init()
					} else {
						m.client = nil
					}
				}
			}
		case "r":
			if m.client != nil {
				return m, fetchSnapshot(m.client, m.timeout)
			}
		}
	}

	return m, nil
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
				return m, m.Init()
			}
		case "tab":
			m.inputField = (m.inputField + 1) % 4
			return m, nil
		case "backspace":
			field := m.getFieldValue()
			if field != nil && len(*field) > 0 {
				*field = (*field)[:len(*field)-1]
			}
		default:
			if msg.Type == tea.KeyRunes {
				field := m.getFieldValue()
				if field != nil {
					*field += msg.String()
				}
			}
		}
	}
	return m, nil
}

func (m *DashboardModel) View() string {
	if m.quitting {
		return ""
	}

	if m.creating || m.editing {
		popup := m.renderInputMode(m.creating)
		return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, popup)
	}

	sizes := calculateContainerSizes(m.width, m.height)
	endpointsPanel := m.renderEndpointsPanel(sizes.Endpoints.Width, sizes.Endpoints.Height)
	metricsGrid := m.renderMetricsGrid(sizes.MetricsGrid.Width, sizes.MetricsGrid.Height)
	dataPanel := m.renderDataPanel(sizes.Data.Width, sizes.Data.Height)
	statusBar := m.renderStatusBar(sizes.StatusBar.Width, sizes.StatusBar.Height)

	leftSide := lipgloss.JoinVertical(lipgloss.Left, endpointsPanel, metricsGrid)
	separator := lipgloss.NewStyle().Foreground(lipgloss.Color("240")).Render("│")
	main := lipgloss.JoinHorizontal(lipgloss.Left, leftSide, separator, dataPanel)
	content := lipgloss.JoinVertical(lipgloss.Left, main, statusBar)

	if m.helpActive {
		helpText := `Keyboard Shortcuts
?:       - Show this help
q, ctrl+c - Quit
j, k      - Navigate endpoints
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

func (m *DashboardModel) renderMetricsGrid(width, height int) string {
	width, height = ensureMinDimensions(width, height, 20, 5)

	gridStyle := lipgloss.NewStyle().
		Border(lipgloss.RoundedBorder()).
		BorderForeground(lipgloss.Color("62")).
		Padding(1, 1).
		Background(lipgloss.Color("0")).
		Width(width).
		Height(height)

	innerHeight := height - 2
	innerWidth := width - 4

	var rows []string
	labelStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("141")).Bold(true)
	valueStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("250"))

	if m.last == nil || m.lastErr != nil {
		rows = []string{
			fmt.Sprintf("%s %s", labelStyle.Render("VRAM Usage:"), valueStyle.Render("--/-- GB (--%%)")),
			fmt.Sprintf("%s %s", labelStyle.Render("Memory Blocks:"), valueStyle.Render("--/-- (Total: --)")),
			fmt.Sprintf("%s %s", labelStyle.Render("Processes:"), valueStyle.Render("-- (none)")),
			fmt.Sprintf("%s %s", labelStyle.Render("Fragmentation:"), valueStyle.Render("--%% (Ratio: --)")),
		}
	} else {
		usedGB := float64(m.last.UsedBytes) / (1024 * 1024 * 1024)
		totalGB := float64(m.last.TotalBytes) / (1024 * 1024 * 1024)
		freeGB := float64(m.last.FreeBytes) / (1024 * 1024 * 1024)
		processCount := len(m.last.Processes)
		activeBlocks := m.last.ActiveBlocks
		freeBlocks := m.last.FreeBlocks
		fragmentation := m.last.FragmentationRatio * 100.0

		rows = []string{
			fmt.Sprintf("%s %s/%s GB %s",
				labelStyle.Render("VRAM Usage:"),
				styleColor(getPercentColor(m.last.UsedPercent)).Render(fmt.Sprintf("%.2f", usedGB)),
				styleColor("250").Render(fmt.Sprintf("%.2f", totalGB)),
				styleColor(getPercentColor(m.last.UsedPercent)).Render(fmt.Sprintf("(%.1f%%)", m.last.UsedPercent))),
			fmt.Sprintf("%s %s GB",
				labelStyle.Render("Free VRAM:"),
				styleColor("46").Render(fmt.Sprintf("%.2f", freeGB))),
			fmt.Sprintf("%s %s/%s %s",
				labelStyle.Render("Memory Blocks:"),
				styleColor("214").Render(fmt.Sprintf("%d", activeBlocks)),
				styleColor("220").Render(fmt.Sprintf("%d", freeBlocks)),
				styleColor("245").Render(fmt.Sprintf("(Total: %d)", activeBlocks+freeBlocks))),
			fmt.Sprintf("%s %s",
				labelStyle.Render("Fragmentation:"),
				styleColor(getPercentColor(fragmentation)).Render(fmt.Sprintf("%.2f%% (Ratio: %.4f)", fragmentation, m.last.FragmentationRatio))),
			fmt.Sprintf("%s %s",
				labelStyle.Render("Processes:"),
				styleColor("39").Render(fmt.Sprintf("%d", processCount))),
		}

		for i, proc := range m.last.Processes {
			if i >= 5 {
				break
			}
			procUsedGB := float64(proc.UsedBytes) / (1024 * 1024 * 1024)
			procName := proc.Name
			if len(procName) > 20 {
				procName = procName[:20] + "..."
			}
			rows = append(rows, fmt.Sprintf("%s %s %s",
				labelStyle.Render(fmt.Sprintf("  PID %d:", proc.PID)),
				styleColor("245").Render(procName),
				styleColor("214").Render(fmt.Sprintf("%.2f GB", procUsedGB))))
		}

		if len(m.last.Threads) > 0 {
			rows = append(rows, fmt.Sprintf("%s %s",
				labelStyle.Render("Threads:"),
				styleColor("39").Render(fmt.Sprintf("%d", len(m.last.Threads)))))
			for i, thread := range m.last.Threads {
				if i >= 3 {
					break
				}
				threadGB := float64(thread.AllocatedBytes) / (1024 * 1024 * 1024)
				rows = append(rows, fmt.Sprintf("%s %s %s",
					labelStyle.Render(fmt.Sprintf("  Thread %d:", thread.ThreadID)),
					styleColor("245").Render(thread.State),
					styleColor("214").Render(fmt.Sprintf("%.2f GB", threadGB))))
			}
		}
	}

	maxVisibleRows := innerHeight
	if maxVisibleRows < 1 {
		maxVisibleRows = 1
	}
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

	rowStyle := lipgloss.NewStyle().Width(innerWidth).Align(lipgloss.Left)
	var gridRows []string
	for _, row := range visibleRows {
		gridRows = append(gridRows, rowStyle.Render(row))
	}

	grid := strings.Join(gridRows, "\n")
	return gridStyle.Render(grid)
}

func (m *DashboardModel) renderTopBar(width int) string {
	width, _ = ensureMinDimensions(width, 1, 10, 1)

	topBarStyle := lipgloss.NewStyle().
		Background(lipgloss.Color("0")).
		Foreground(lipgloss.Color("250")).
		Padding(0, 1).
		Width(width).
		Height(1)

	if m.last == nil || m.lastErr != nil {
		return topBarStyle.Render("VRAM: N/A  Used: --/-- GB  Free: -- GB  Processes: --  Blocks: --/--  Fragmentation: --%")
	}

	usedGB := float64(m.last.UsedBytes) / (1024 * 1024 * 1024)
	totalGB := float64(m.last.TotalBytes) / (1024 * 1024 * 1024)
	freeGB := float64(m.last.FreeBytes) / (1024 * 1024 * 1024)
	processCount := len(m.last.Processes)
	activeBlocks := m.last.ActiveBlocks
	freeBlocks := m.last.FreeBlocks
	fragmentation := m.last.FragmentationRatio * 100.0

	valueStyle := styleColor("250").Bold(true)
	info := fmt.Sprintf("VRAM: %s  Used: %s/%s GB  Free: %s GB  Processes: %s  Blocks: %s/%s  Fragmentation: %s%%",
		valueStyle.Render("GPU"),
		styleColor(getPercentColor(m.last.UsedPercent)).Render(fmt.Sprintf("%.2f", usedGB)),
		styleColor("250").Render(fmt.Sprintf("%.2f", totalGB)),
		styleColor("46").Render(fmt.Sprintf("%.2f", freeGB)),
		styleColor("39").Render(fmt.Sprintf("%d", processCount)),
		styleColor("214").Render(fmt.Sprintf("%d", activeBlocks)),
		styleColor("220").Render(fmt.Sprintf("%d", freeBlocks)),
		styleColor(getPercentColor(fragmentation)).Render(fmt.Sprintf("%.2f", fragmentation)))

	return topBarStyle.Render(info)
}

func (m *DashboardModel) renderEndpointsPanel(width, height int) string {
	width, height = ensureMinDimensions(width, height, 10, 3)

	var b strings.Builder
	headerBg := lipgloss.NewStyle().Background(lipgloss.Color("0"))
	headerStyle := lipgloss.NewStyle().Bold(true).Foreground(lipgloss.Color("141"))
	header := headerBg.Render(headerStyle.Render("Endpoints")) + "\n"
	b.WriteString(header)
	b.WriteString("\n")

	innerHeight := height - 3
	if innerHeight < 1 {
		innerHeight = 1
	}

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
			selectedStyle := lipgloss.NewStyle().
				Background(lipgloss.Color("63")).
				Foreground(lipgloss.Color("231")).
				Bold(true).
				Width(availableWidth).
				Align(lipgloss.Left)
			b.WriteString(selectedStyle.Render(name) + "\n")
		} else {
			style := lipgloss.NewStyle().
				Background(lipgloss.Color("0")).
				Foreground(lipgloss.Color("245")).
				Width(availableWidth).
				Align(lipgloss.Left)
			b.WriteString(style.Render(name) + "\n")
		}
	}

	m.fillToHeight(&b, b.String(), width, height, "0")
	return endpointBoxStyle.Width(width).Height(height).Render(b.String())
}

func (m *DashboardModel) renderDataPanel(width, height int) string {
	if m.client == nil {
		return m.renderEmptyState(width, height, "No endpoint selected\n\nPress 'n' to create one")
	}

	if !m.loaded {
		return m.renderEmptyState(width, height, "Loading...")
	}

	if m.lastErr != nil && m.last == nil {
		return m.renderEmptyState(width, height, fmt.Sprintf("Error: %s\n\nPress 'r' to retry", m.lastErr.Error()))
	}

	innerHeight := height - 2
	availableHeight := innerHeight - 2
	boxHeight := availableHeight / 3
	if boxHeight < 5 {
		boxHeight = 5
	}

	usedMB := int(m.last.UsedBytes / (1024 * 1024))
	totalMB := int(m.last.TotalBytes / (1024 * 1024))
	vramContent := m.renderMetricContent("VRAM Usage", boxHeight, width, usedMB, totalMB, 0, m.getVRAMHistory(), redColor, 100.0)
	blocksContent := m.renderMetricContent("Memory Blocks", boxHeight, width, m.last.ActiveBlocks, m.last.FreeBlocks, 0, m.getBlocksHistory(), blueColor, 0)
	fragmentationContent := m.renderMetricContent("Fragmentation", boxHeight, width, int(m.last.FragmentationRatio*100), 100, 0, m.getFragmentationHistory(), yellowColor, 100.0)

	vramLines := strings.TrimRight(vramContent, "\n")
	blocksLines := strings.TrimRight(blocksContent, "\n")
	fragmentationLines := strings.TrimRight(fragmentationContent, "\n")

	emptyLine := lipgloss.NewStyle().Background(lipgloss.Color("0")).Render(strings.Repeat(" ", max(0, width-2)))
	combined := strings.Join([]string{vramLines, emptyLine, blocksLines, emptyLine, fragmentationLines}, "\n")

	return metricBoxStyle.Width(width).Height(height).Render(combined)
}

func (m *DashboardModel) renderEmptyState(width, height int, message string) string {
	width, height = ensureMinDimensions(width, height, 10, 3)

	var b strings.Builder
	if m.lastErr != nil || message == "Loading..." {
		b.WriteString("\n")
	}
	emptyStyle := lipgloss.NewStyle().
		Foreground(lipgloss.Color("240")).
		Italic(true).
		Align(lipgloss.Center).
		Width(width - 4)
	b.WriteString(emptyStyle.Render(message))
	m.fillToHeight(&b, b.String(), width, height-2, "0")
	return metricBoxStyle.Width(width).Height(height).Render(b.String())
}

func (m *DashboardModel) renderMetricContent(title string, height, width int, val1, val2, val3 int, history []float64, color lipgloss.Color, fixedMax float64) string {
	width, height = ensureMinDimensions(width, height, 10, 5)

	var b strings.Builder
	titleStyle := lipgloss.NewStyle().Foreground(color).Bold(true)
	valuesText := m.formatMetricValues(title, val1, val2, val3)
	topRow := fmt.Sprintf("%s  %s", titleStyle.Render(title), valuesText)
	b.WriteString(topRow + "\n")

	if len(history) >= 2 {
		chartHeight := height - 1
		if chartHeight < 4 {
			chartHeight = 4
		}
		chartOutput := m.renderSparklineChart(history, width-2, chartHeight, color, fixedMax)
		b.WriteString(chartOutput)
	} else {
		loadingStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("240")).Italic(true)
		b.WriteString(loadingStyle.Render("Collecting data...") + "\n")
	}

	content := b.String()
	lines := strings.Split(strings.TrimRight(content, "\n"), "\n")
	fillWidth := max(0, width-2)
	bgFill := lipgloss.NewStyle().Background(lipgloss.Color("0")).Render(strings.Repeat(" ", fillWidth))
	for i := len(lines); i < height; i++ {
		b.WriteString(bgFill)
		if i < height-1 {
			b.WriteString("\n")
		}
	}

	return b.String()
}

func (m *DashboardModel) renderSparklineChart(values []float64, width, height int, color lipgloss.Color, fixedMax float64) string {
	if len(values) < 2 {
		return ""
	}
	width, height = ensureMinDimensions(width, height, 20, 6)

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

	chartWidth := width
	if chartWidth < 10 {
		chartWidth = 10
	}
	chartHeight := height
	if chartHeight < 4 {
		chartHeight = 4
	}
	gridHeight := chartHeight - 1
	if gridHeight < 3 {
		gridHeight = 3
	}

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
	legendStyle := lipgloss.NewStyle().Foreground(lipgloss.Color("240")).Background(lipgloss.Color("0"))

	currentVal := values[len(values)-1]
	legendMax := maxVal
	if fixedMax > 0 {
		legendMax = fixedMax
	}

	legendText := fmt.Sprintf("Max: %s  Now: %s  Min: %s",
		formatChartNumber(legendMax), formatChartNumber(currentVal), formatChartNumber(minVal))

	if chartHeight > 0 && chartWidth > len(legendText) {
		b.WriteString(legendStyle.Render(legendText))
		remaining := chartWidth - len(legendText)
		if remaining > 0 {
			b.WriteString(strings.Repeat(" ", remaining))
		}
		b.WriteString("\n")

		for i := 0; i < gridHeight && i < len(grid); i++ {
			b.WriteString(colorStyle.Render(string(grid[i])))
			b.WriteString("\n")
		}
	} else {
		for i := 0; i < chartHeight && i < len(grid); i++ {
			b.WriteString(colorStyle.Render(string(grid[i])))
			b.WriteString("\n")
		}
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

func (m *DashboardModel) renderStatusBar(width, height int) string {
	width, height = ensureMinDimensions(width, height, 10, 1)

	helpText := lipgloss.NewStyle().Foreground(lipgloss.Color("245")).Render("?: help")
	leftText := lipgloss.NewStyle().Foreground(lipgloss.Color("245")).Render("n: new  e: edit  d: delete  q: quit")
	leftContent := helpText + "  " + leftText

	star := lipgloss.NewStyle().Foreground(lipgloss.Color("220")).Render("★")
	githubURL := "https://github.com/maxdcmn/blackbox"
	githubTextStyled := lipgloss.NewStyle().Foreground(lipgloss.Color("220")).Underline(true).Bold(true).Render("GitHub")
	githubTextLinked := fmt.Sprintf("\x1b]8;;%s\x1b\\%s\x1b]8;;\x1b\\", githubURL, githubTextStyled)
	versionV := lipgloss.NewStyle().Foreground(lipgloss.Color("46")).Bold(true).Render("v")
	versionNum := lipgloss.NewStyle().Foreground(lipgloss.Color("46")).Render(version)
	rightContent := star + " " + githubTextLinked + "  " + versionV + versionNum

	availableWidth := width - 2
	leftLen := lipgloss.Width(leftContent)
	rightLen := lipgloss.Width(rightContent)
	spacerLen := availableWidth - leftLen - rightLen
	if spacerLen < 1 {
		spacerLen = 1
	}

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
		b.WriteString(*field)
		if i == m.inputField {
			b.WriteString("█")
		}
		b.WriteString("\n")
	}

	b.WriteString("\nTab: next field  Enter: save  Esc: cancel")
	return popupStyle.Width(60).Render(b.String())
}

func (m *DashboardModel) getFieldValue() *string {
	fields := []*string{&m.newName, &m.newURL, &m.newEp, &m.newTO}
	if m.inputField >= 0 && m.inputField < len(fields) {
		return fields[m.inputField]
	}
	return nil
}

func (m *DashboardModel) formatMetricValues(title string, val1, val2, val3 int) string {
	if title == "VRAM Usage" {
		percent := 0.0
		if val2 > 0 {
			percent = (float64(val1) / float64(val2)) * 100.0
		}
		return fmt.Sprintf("%s %s",
			styleColor(getPercentColor(percent)).Render(fmt.Sprintf("%d/%d MB", val1, val2)),
			styleColor(getPercentColor(percent)).Render(fmt.Sprintf("(%.1f%%)", percent)))
	} else if title == "Memory Blocks" {
		return fmt.Sprintf("%s  %s",
			styleColor("39").Render(fmt.Sprintf("Active: %d", val1)),
			styleColor("46").Render(fmt.Sprintf("Free: %d", val2)))
	} else if title == "Fragmentation" {
		return styleColor(getPercentColor(float64(val1))).Render(fmt.Sprintf("%.2f%%", float64(val1)))
	} else {
		percent := 0.0
		if val2 > 0 {
			percent = (float64(val1) / float64(val2)) * 100.0
		}
		return fmt.Sprintf("%s %s",
			styleColor("196").Render(fmt.Sprintf("%d/%d", val1, val2)),
			styleColor("196").Render(fmt.Sprintf("(%.1f%%)", percent)))
	}
}

func styleColor(color string) lipgloss.Style {
	return lipgloss.NewStyle().Foreground(lipgloss.Color(color))
}

func formatChartNumber(v float64) string {
	if v >= 1000000 {
		return fmt.Sprintf("%.2fM", v/1000000)
	} else if v >= 1000 {
		return fmt.Sprintf("%.1fK", v/1000)
	} else if v >= 100 {
		return fmt.Sprintf("%.0f", v)
	} else if v >= 10 {
		return fmt.Sprintf("%.1f", v)
	}
	return fmt.Sprintf("%.2f", v)
}

func (m *DashboardModel) fillToHeight(b *strings.Builder, content string, width, targetHeight int, bgColor string) {
	lines := strings.Split(content, "\n")
	fillWidth := max(0, width-4)
	bgFill := lipgloss.NewStyle().Background(lipgloss.Color(bgColor)).Render(strings.Repeat(" ", fillWidth))
	for i := len(lines); i < targetHeight; i++ {
		b.WriteString(bgFill + "\n")
	}
}

func formatLargeNumber(n int64) string {
	if n >= 1000000000 {
		return fmt.Sprintf("%.2fB", float64(n)/1000000000)
	} else if n >= 1000000 {
		return fmt.Sprintf("%.2fM", float64(n)/1000000)
	} else if n >= 1000 {
		return fmt.Sprintf("%.1fK", float64(n)/1000)
	}
	return fmt.Sprintf("%d", n)
}

func getPercentColor(percent float64) string {
	if percent >= 90 {
		return "196"
	} else if percent >= 70 {
		return "214"
	} else if percent >= 50 {
		return "220"
	}
	return "46"
}

var (
	endpointBoxStyle = lipgloss.NewStyle().
				Border(lipgloss.RoundedBorder()).
				BorderForeground(lipgloss.Color("63")).
				Padding(0, 1).
				Background(lipgloss.Color("0"))

	metricBoxStyle = lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(lipgloss.Color("62")).
			Padding(0, 1).
			Background(lipgloss.Color("0"))

	statusBarStyle = lipgloss.NewStyle().
			Height(1).
			Foreground(lipgloss.Color("250")).
			Background(lipgloss.Color("0")).
			Padding(0, 1).
			Bold(false)

	popupStyle = lipgloss.NewStyle().
			Border(lipgloss.RoundedBorder()).
			BorderForeground(lipgloss.Color("141")).
			Padding(1, 2).
			Background(lipgloss.Color("0")).
			Foreground(lipgloss.Color("250"))

	fieldStyle = lipgloss.NewStyle().
			Foreground(lipgloss.Color("245"))

	activeFieldStyle = lipgloss.NewStyle().
				Background(lipgloss.Color("63")).
				Foreground(lipgloss.Color("231")).
				Bold(true)

	blueColor   = lipgloss.Color("39")
	yellowColor = lipgloss.Color("220")
	redColor    = lipgloss.Color("196")
)
