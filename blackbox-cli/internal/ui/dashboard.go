package ui

import (
	"context"
	"time"

	"github.com/maxdcmn/blackbox-cli/internal/client"
	"github.com/maxdcmn/blackbox-cli/internal/config"
	"github.com/maxdcmn/blackbox-cli/internal/model"
	"github.com/maxdcmn/blackbox-cli/internal/utils"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

type DataPoint struct {
	Time               time.Time
	AllocatedVRAMBytes int64
	UsedKVCacheBytes   int64
	PrefixCacheHitRate float64
}

type DashboardModel struct {
	config                  *config.Config
	endpoints               []config.Endpoint
	selected                int
	client                  *client.Client
	interval                time.Duration
	timeout                 time.Duration
	width                   int
	height                  int
	last                    *model.Snapshot
	lastErr                 error
	loaded                  bool
	history                 []DataPoint
	quitting                bool
	creating                bool
	editing                 bool
	deploying               bool
	helpActive              bool
	showingModels           bool
	spindowning             bool
	optimizing              bool
	newName                 string
	inputField              int
	newURL                  string
	newEp                   string
	newTO                   string
	editOldName             string
	deployModelID           string
	deployHFToken           string
	deployPort              string
	deployMessage           string
	deploySuccess           bool
	modelsList              *client.ModelsResponse
	modelsErr               error
	selectedModel           int
	spindownMessage         string
	spindownSuccess         bool
	spindownInFlight        bool
	optimizeMessage         string
	optimizeSuccess         bool
	optimizeRestartedModels []string
	cursorPos               [4]int
	metricsScroll           int
	endpointsScroll         int
	modelsScroll            int
	fetchSequence           int
	focusedPanel            int
	maxVRAMSeen             float64
	maxBlocksSeen           float64
	maxFragSeen             float64
	maxPrefixHitRateSeen    float64
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
	timeout, err := time.ParseDuration(ep.Timeout)
	if err != nil || timeout == 0 {
		// Fallback to model's timeout if endpoint timeout is invalid or zero
		timeout = m.timeout
		if timeout == 0 {
			timeout = 10 * time.Second // Final fallback
		}
	}
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
type streamMsg struct {
	s          *model.Snapshot
	err        error
	endpointID int
}

func (m *DashboardModel) Init() tea.Cmd {
	if m.client == nil {
		return nil
	}
	m.fetchSequence++
	return startPolling(m.client, m.selected, m.fetchSequence)
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

func startPolling(c *client.Client, endpointID int, fetchSeq int) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
		defer cancel()
		aggSnap, err := c.AggregatedSnapshot(ctx, 5)
		if err != nil {
			return streamMsg{s: nil, err: err, endpointID: endpointID}
		}
		// Convert aggregated snapshot to regular snapshot using average values
		// Calculate total used KV cache from models to ensure consistency
		var totalUsedKV int64
		for _, m := range aggSnap.Models {
			utils.Debug("Model %s: UsedKVCacheBytes=%d, AllocatedVRAMBytes=%d", m.ModelID, m.UsedKVCacheBytes, m.AllocatedVRAMBytes)
			totalUsedKV += m.UsedKVCacheBytes
		}
		utils.Debug("Total from models: %d, Aggregated avg: %.2f, Sample count: %d", totalUsedKV, aggSnap.UsedKVCacheBytes.Avg, aggSnap.UsedKVCacheBytes.Count)
		// Use the calculated total from models, or fallback to aggregated avg if sum is 0
		if totalUsedKV == 0 {
			totalUsedKV = int64(aggSnap.UsedKVCacheBytes.Avg)
			utils.Debug("Using aggregated avg as fallback: %d", totalUsedKV)
		}
		s := &model.Snapshot{
			TotalVRAMBytes:     aggSnap.TotalVRAMBytes,
			AllocatedVRAMBytes:  int64(aggSnap.AllocatedVRAMBytes.Avg),
			UsedKVCacheBytes:    totalUsedKV,
			PrefixCacheHitRate:  aggSnap.PrefixCacheHitRate.Avg,
			Models:              aggSnap.Models,
		}
		utils.Debug("Final snapshot: UsedKVCacheBytes=%d, Models count=%d", s.UsedKVCacheBytes, len(s.Models))
		return streamMsg{s: s, err: nil, endpointID: endpointID}
	}
}

func scheduleNextPoll(c *client.Client, endpointID int) tea.Cmd {
	return tea.Tick(5*time.Second, func(time.Time) tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
		defer cancel()
		aggSnap, err := c.AggregatedSnapshot(ctx, 5)
		if err != nil {
			return streamMsg{s: nil, err: err, endpointID: endpointID}
		}
		// Convert aggregated snapshot to regular snapshot using average values
		// Calculate total used KV cache from models to ensure consistency
		var totalUsedKV int64
		for _, m := range aggSnap.Models {
			utils.Debug("Model %s: UsedKVCacheBytes=%d, AllocatedVRAMBytes=%d", m.ModelID, m.UsedKVCacheBytes, m.AllocatedVRAMBytes)
			totalUsedKV += m.UsedKVCacheBytes
		}
		utils.Debug("Total from models: %d, Aggregated avg: %.2f, Sample count: %d", totalUsedKV, aggSnap.UsedKVCacheBytes.Avg, aggSnap.UsedKVCacheBytes.Count)
		// Use the calculated total from models, or fallback to aggregated avg if sum is 0
		if totalUsedKV == 0 {
			totalUsedKV = int64(aggSnap.UsedKVCacheBytes.Avg)
			utils.Debug("Using aggregated avg as fallback: %d", totalUsedKV)
		}
		s := &model.Snapshot{
			TotalVRAMBytes:     aggSnap.TotalVRAMBytes,
			AllocatedVRAMBytes:  int64(aggSnap.AllocatedVRAMBytes.Avg),
			UsedKVCacheBytes:    totalUsedKV,
			PrefixCacheHitRate:  aggSnap.PrefixCacheHitRate.Avg,
			Models:              aggSnap.Models,
		}
		utils.Debug("Final snapshot: UsedKVCacheBytes=%d, Models count=%d", s.UsedKVCacheBytes, len(s.Models))
		return streamMsg{s: s, err: nil, endpointID: endpointID}
	})
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
	if m.deploying {
		return m.updateDeployMode(msg)
	}
	if m.showingModels {
		return m.updateModelsMode(msg)
	}
	if m.spindowning {
		return m.updateSpindownMode(msg)
	}
	if m.optimizing {
		return m.updateOptimizeMode(msg)
	}

	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width, m.height = msg.Width, msg.Height
		return m, nil

	case tickMsg:
		// No longer used with SSE, but keeping for compatibility
		return m, nil

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

	case streamMsg:
		if msg.endpointID != m.selected {
			return m, nil
		}
		m.loaded = true
		m.lastErr = msg.err
		if msg.err == nil && msg.s != nil {
			m.updateHistory(msg.s)
		}
		// Schedule next poll in 5 seconds
		return m, scheduleNextPoll(m.client, m.selected)

	case tea.KeyMsg:
		return m.handleKey(msg)
	}

	return m, nil
}

func (m *DashboardModel) updateHistory(s *model.Snapshot) {
	m.last = s
	m.history = append(m.history, DataPoint{
		Time:               time.Now(),
		AllocatedVRAMBytes: s.AllocatedVRAMBytes,
		UsedKVCacheBytes:   s.UsedKVCacheBytes,
		PrefixCacheHitRate: s.PrefixCacheHitRate,
	})
	if len(m.history) > maxHistorySize {
		m.history = m.history[1:]
	}

	// Track max values for scaling charts
	allocatedGB := float64(s.AllocatedVRAMBytes) / (1024 * 1024 * 1024)
	if allocatedGB > m.maxVRAMSeen {
		m.maxVRAMSeen = allocatedGB
	}

	usedKVCacheGB := float64(s.UsedKVCacheBytes) / (1024 * 1024 * 1024)
	if usedKVCacheGB > m.maxBlocksSeen {
		m.maxBlocksSeen = usedKVCacheGB
	}

	if s.PrefixCacheHitRate > m.maxPrefixHitRateSeen {
		m.maxPrefixHitRateSeen = s.PrefixCacheHitRate
	}
}

func (m *DashboardModel) handleKey(msg tea.KeyMsg) (tea.Model, tea.Cmd) {
	key := msg.String()
	if m.creating || m.editing || m.deploying || m.helpActive || m.showingModels || m.spindowning || m.optimizing {
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
		m.newURL = "http://127.0.0.1:6767"
		m.newEp = "/vram"
		m.newTO = "10s"
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
					return m, startPolling(m.client, m.selected, m.fetchSequence)
				}
				m.client = nil
			}
		}
	case "r":
		if m.client != nil {
			m.loaded = false
			m.lastErr = nil
			m.fetchSequence++
			return m, startPolling(m.client, m.selected, m.fetchSequence)
		}
	case "D":
		// Deploy model - only if we have an endpoint selected
		if m.client != nil && len(m.endpoints) > 0 && m.selected < len(m.endpoints) {
			m.deploying = true
			m.deployModelID = ""
			m.deployHFToken = ""
			m.deployPort = ""
			m.deployMessage = ""
			m.deploySuccess = false
			m.inputField = 0
			m.cursorPos = [4]int{0, 0, 0, 0}
			return m, nil
		}
	case "m":
		// Show models list
		if m.client != nil && len(m.endpoints) > 0 && m.selected < len(m.endpoints) {
			m.showingModels = true
			m.modelsList = nil
			m.modelsErr = nil
			m.selectedModel = 0
			m.modelsScroll = 0
			ep := m.endpoints[m.selected]
			modelsClient := client.New(ep.BaseURL, ep.Endpoint, m.timeout)
			return m, fetchModels(modelsClient, m.timeout)
		}
	case "s":
		// Spindown model - show models list first
		if m.client != nil && len(m.endpoints) > 0 && m.selected < len(m.endpoints) {
			m.spindowning = true
			m.modelsList = nil
			m.modelsErr = nil
			m.selectedModel = 0
			m.modelsScroll = 0
			m.spindownMessage = ""
			m.spindownSuccess = false
			m.spindownInFlight = false
			ep := m.endpoints[m.selected]
			modelsClient := client.New(ep.BaseURL, ep.Endpoint, m.timeout)
			return m, fetchModels(modelsClient, m.timeout)
		}
	case "o":
		// Optimize models
		if m.client != nil && len(m.endpoints) > 0 && m.selected < len(m.endpoints) {
			m.optimizing = true
			m.optimizeMessage = ""
			m.optimizeSuccess = false
			ep := m.endpoints[m.selected]
			optimizeClient := client.New(ep.BaseURL, ep.Endpoint, m.timeout)
			return m, optimizeModels(optimizeClient, m.timeout)
		}
	}
	return m, nil
}

func (m *DashboardModel) handleDown() (tea.Model, tea.Cmd) {
	if m.focusedPanel == 1 {
		if m.last != nil {
			// Calculate total rows: 2 base rows + per-model rows (2 per model)
			baseRows := 2
			modelRows := len(m.last.Models) * 2
			totalRows := baseRows + modelRows
			sizes := calculateContainerSizes(m.width, m.height)
			maxVisibleRows := sizes.MetricsGrid.Height - 2
			if totalRows > maxVisibleRows && m.metricsScroll < totalRows-maxVisibleRows {
				m.metricsScroll++
				return m, nil
			}
		}
	} else if m.focusedPanel == 0 && m.selected < len(m.endpoints)-1 {
		m.selectEndpoint(m.selected + 1)
		return m, startPolling(m.client, m.selected, m.fetchSequence)
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
		return m, startPolling(m.client, m.selected, m.fetchSequence)
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
	if m.deploying {
		return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, m.renderDeployMode())
	}
	if m.showingModels {
		return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, m.renderModelsMode())
	}
	if m.spindowning {
		return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, m.renderSpindownMode())
	}
	if m.optimizing {
		return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, m.renderOptimizeMode())
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
D         - Deploy model
m         - List models
s         - Spindown model
o         - Optimize models
r         - Refresh data
Press any key to close`
		popup := popupStyle.Width(50).Render(helpText)
		popup = lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, popup)
		return lipgloss.JoinVertical(lipgloss.Left, content, popup)
	}

	return content
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
		return float64(dp.AllocatedVRAMBytes) / (1024 * 1024 * 1024) // Convert to GB
	})
}

func (m *DashboardModel) getBlocksHistory() []float64 {
	return m.getHistory(func(dp DataPoint) float64 {
		return float64(dp.UsedKVCacheBytes) / (1024 * 1024 * 1024) // Convert to GB
	})
}

func (m *DashboardModel) getFragmentationHistory() []float64 {
	// Not used anymore, but keeping for compatibility
	return []float64{}
}

func (m *DashboardModel) getPrefixCacheHitRateHistory() []float64 {
	return m.getHistory(func(dp DataPoint) float64 {
		return dp.PrefixCacheHitRate
	})
}
