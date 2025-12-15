package ui

import (
	"context"
	"time"

	"github.com/maxdcmn/blackbox-cli/internal/client"
	"github.com/maxdcmn/blackbox-cli/internal/config"
	"github.com/maxdcmn/blackbox-cli/internal/model"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/lipgloss"
)

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
					return m, fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence)
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
		return m, fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence)
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
		return m, fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence)
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
