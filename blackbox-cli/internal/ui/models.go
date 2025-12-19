package ui

import (
	"context"
	"fmt"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/maxdcmn/blackbox-cli/internal/client"
)

type modelsMsg struct {
	models *client.ModelsResponse
	err    error
}

type spindownMsg struct {
	success bool
	message string
}

type optimizeMsg struct {
	success        bool
	message        string
	restartedModels []string
}

func fetchModels(c *client.Client, timeout time.Duration) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), timeout)
		defer cancel()
		models, err := c.ListModels(ctx)
		return modelsMsg{models: models, err: err}
	}
}

func spindownModel(c *client.Client, timeout time.Duration, modelID string) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), timeout)
		defer cancel()
		resp, err := c.SpindownModel(ctx, modelID, "")
		if err != nil {
			return spindownMsg{success: false, message: err.Error()}
		}
		return spindownMsg{success: resp.Success, message: resp.Message}
	}
}

func optimizeModels(c *client.Client, timeout time.Duration) tea.Cmd {
	return func() tea.Msg {
		ctx, cancel := context.WithTimeout(context.Background(), timeout*5)
		defer cancel()
		resp, err := c.Optimize(ctx)
		if err != nil {
			return optimizeMsg{success: false, message: err.Error()}
		}
		return optimizeMsg{
			success:        resp.Success,
			message:        resp.Message,
			restartedModels: resp.RestartedModels,
		}
	}
}

func (m *DashboardModel) renderModelsMode() string {
	var b strings.Builder
	b.WriteString("Deployed Models\n\n")

	if m.modelsErr != nil {
		b.WriteString(styleColor(colorRed).Render("✗ Error: " + m.modelsErr.Error()))
		// Try to show models from snapshot as fallback
		if m.last != nil && len(m.last.Models) > 0 {
			b.WriteString("\n\nShowing models from VRAM tracking:\n\n")
			for i, model := range m.last.Models {
				selected := i == m.selectedModel
				line := fmt.Sprintf("● %s (port: %d)", model.ModelID, model.Port)
				if selected {
					line = activeFieldStyle.Render("> " + line)
				} else {
					line = "  " + line
				}
				b.WriteString(line + "\n")
			}
		}
		b.WriteString("\n\nPress Esc to close")
		return popupStyle.Width(80).Render(b.String())
	}

	if m.modelsList == nil {
		b.WriteString("Loading...")
		return popupStyle.Width(80).Render(b.String())
	}

	if len(m.modelsList.Models) == 0 {
		// Try to show models from snapshot as fallback
		if m.last != nil && len(m.last.Models) > 0 {
			b.WriteString("Note: Models from Docker not available, showing from VRAM tracking:\n\n")
			for i, model := range m.last.Models {
				selected := i == m.selectedModel
				line := fmt.Sprintf("● %s (port: %d)", model.ModelID, model.Port)
				if selected {
					line = activeFieldStyle.Render("> " + line)
				} else {
					line = "  " + line
				}
				b.WriteString(line + "\n")
			}
			b.WriteString("\n\nPress Esc to close")
			return popupStyle.Width(80).Render(b.String())
		}
		b.WriteString("No models deployed")
		b.WriteString("\n\nPress Esc to close")
		return popupStyle.Width(80).Render(b.String())
	}

	b.WriteString(fmt.Sprintf("Total: %d | Running: %d | Max: %d\n\n", m.modelsList.Total, m.modelsList.Running, m.modelsList.MaxAllowed))

	maxVisible := 10
	start := m.modelsScroll
	end := start + maxVisible
	if end > len(m.modelsList.Models) {
		end = len(m.modelsList.Models)
	}

	for i := start; i < end; i++ {
		model := m.modelsList.Models[i]
		selected := i == m.selectedModel
		status := "●"
		statusColor := colorGreen
		if !model.Running {
			status = "○"
			statusColor = colorRed
		}

		line := fmt.Sprintf("%s %s (port: %d)", styleColor(statusColor).Render(status), model.ModelID, model.Port)
		if selected {
			line = activeFieldStyle.Render("> " + line)
		} else {
			line = "  " + line
		}
		b.WriteString(line + "\n")
	}

	if len(m.modelsList.Models) > maxVisible {
		b.WriteString(fmt.Sprintf("\n[%d-%d of %d]", start+1, end, len(m.modelsList.Models)))
	}

	b.WriteString("\n\nj/k: navigate  Esc: close")
	return popupStyle.Width(80).Height(20).Render(b.String())
}

func (m *DashboardModel) renderSpindownMode() string {
	var b strings.Builder
	b.WriteString("Spindown Model\n\n")

	if m.modelsErr != nil {
		b.WriteString(styleColor(colorRed).Render("✗ Error: " + m.modelsErr.Error()))
		b.WriteString("\n\nPress Esc to close")
		return popupStyle.Width(80).Render(b.String())
	}

	if m.modelsList == nil {
		// Try to show models from snapshot as fallback
		if m.last != nil && len(m.last.Models) > 0 {
			b.WriteString("Note: Using models from VRAM tracking:\n\n")
			for i, model := range m.last.Models {
				selected := i == m.selectedModel
				line := fmt.Sprintf("● %s (port: %d)", model.ModelID, model.Port)
				if selected {
					line = activeFieldStyle.Render("> " + line)
				} else {
					line = "  " + line
				}
				b.WriteString(line + "\n")
			}
			b.WriteString("\n\nj/k: navigate  Enter: spindown  Esc: cancel")
			return popupStyle.Width(80).Height(20).Render(b.String())
		}
		b.WriteString("Loading models...")
		return popupStyle.Width(80).Render(b.String())
	}

	if len(m.modelsList.Models) == 0 {
		// Try to show models from snapshot as fallback
		if m.last != nil && len(m.last.Models) > 0 {
			b.WriteString("Note: Models from Docker not available, showing from VRAM tracking:\n\n")
			for i, model := range m.last.Models {
				selected := i == m.selectedModel
				line := fmt.Sprintf("● %s (port: %d)", model.ModelID, model.Port)
				if selected {
					line = activeFieldStyle.Render("> " + line)
				} else {
					line = "  " + line
				}
				b.WriteString(line + "\n")
			}
			b.WriteString("\n\nj/k: navigate  Enter: spindown  Esc: cancel")
			return popupStyle.Width(80).Height(20).Render(b.String())
		}
		b.WriteString("No models to spindown")
		b.WriteString("\n\nPress Esc to close")
		return popupStyle.Width(80).Render(b.String())
	}

	b.WriteString("Select a model to spindown:\n\n")

	maxVisible := 10
	start := m.modelsScroll
	end := start + maxVisible
	if end > len(m.modelsList.Models) {
		end = len(m.modelsList.Models)
	}

	for i := start; i < end; i++ {
		model := m.modelsList.Models[i]
		selected := i == m.selectedModel
		status := "●"
		statusColor := colorGreen
		if !model.Running {
			status = "○"
			statusColor = colorRed
		}

		line := fmt.Sprintf("%s %s (port: %d)", styleColor(statusColor).Render(status), model.ModelID, model.Port)
		if selected {
			line = activeFieldStyle.Render("> " + line)
		} else {
			line = "  " + line
		}
		b.WriteString(line + "\n")
	}

	if len(m.modelsList.Models) > maxVisible {
		b.WriteString(fmt.Sprintf("\n[%d-%d of %d]", start+1, end, len(m.modelsList.Models)))
	}

	if m.spindownInFlight && m.spindownMessage == "" {
		b.WriteString("\n\n")
		b.WriteString(styleColor(colorOrange).Render("⏳ Spindowning model..."))
	} else if m.spindownMessage != "" {
		b.WriteString("\n\n")
		if m.spindownSuccess {
			b.WriteString(styleColor(colorGreen).Render("✓ " + m.spindownMessage))
		} else {
			b.WriteString(styleColor(colorRed).Render("✗ " + m.spindownMessage))
		}
	}

	b.WriteString("\n\nj/k: navigate  Enter: spindown  Esc: cancel")
	return popupStyle.Width(80).Height(20).Render(b.String())
}

func (m *DashboardModel) renderOptimizeMode() string {
	var b strings.Builder
	b.WriteString("Optimize Models\n\n")

	if m.optimizeMessage == "" {
		b.WriteString("Optimizing GPU utilization...")
		return popupStyle.Width(60).Render(b.String())
	}

	if m.optimizeSuccess {
		b.WriteString(styleColor(colorGreen).Render("✓ " + m.optimizeMessage))
		if len(m.optimizeRestartedModels) > 0 {
			b.WriteString("\n\nRestarted models:")
			for _, model := range m.optimizeRestartedModels {
				b.WriteString("\n  - " + model)
			}
		}
	} else {
		b.WriteString(styleColor(colorRed).Render("✗ " + m.optimizeMessage))
	}

	b.WriteString("\n\nPress Esc to close")
	return popupStyle.Width(60).Render(b.String())
}

func (m *DashboardModel) updateModelsMode(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case modelsMsg:
		m.modelsList = msg.models
		m.modelsErr = msg.err
		return m, nil

	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.showingModels = false
			m.modelsList = nil
			m.modelsErr = nil
			return m, nil
		case "j", "down":
			if m.modelsList != nil && m.selectedModel < len(m.modelsList.Models)-1 {
				m.selectedModel++
				if m.selectedModel >= m.modelsScroll+10 {
					m.modelsScroll++
				}
			}
			return m, nil
		case "k", "up":
			if m.selectedModel > 0 {
				m.selectedModel--
				if m.selectedModel < m.modelsScroll {
					m.modelsScroll--
				}
			}
			return m, nil
		case "s":
			// Switch to spindown mode
			m.showingModels = false
			m.spindowning = true
			m.spindownMessage = ""
			m.spindownSuccess = false
			return m, nil
		}
	}
	return m, nil
}

func (m *DashboardModel) updateSpindownMode(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case modelsMsg:
		m.modelsList = msg.models
		m.modelsErr = msg.err
		return m, nil

	case spindownMsg:
		m.spindownInFlight = false
		m.spindownMessage = msg.message
		m.spindownSuccess = msg.success
		if msg.success {
			m.modelsList = nil
			m.fetchSequence++
			return m, fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence)
		}
		return m, nil

	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.spindowning = false
			m.modelsList = nil
			m.modelsErr = nil
			m.spindownMessage = ""
			m.spindownSuccess = false
			m.spindownInFlight = false
			return m, nil
		case "enter":
			var modelID string
			if m.modelsList != nil && m.selectedModel < len(m.modelsList.Models) {
				modelID = m.modelsList.Models[m.selectedModel].ModelID
			} else if m.last != nil && m.selectedModel < len(m.last.Models) {
				// Fallback to VRAM tracking models
				modelID = m.last.Models[m.selectedModel].ModelID
			}
			if modelID != "" && !m.spindownInFlight {
				m.spindownInFlight = true
				m.spindownMessage = ""
				m.spindownSuccess = false
				ep := m.endpoints[m.selected]
				spindownClient := client.New(ep.BaseURL, ep.Endpoint, m.timeout)
				return m, spindownModel(spindownClient, m.timeout, modelID)
			}
			return m, nil
		case "j", "down":
			if m.modelsList != nil && m.selectedModel < len(m.modelsList.Models)-1 {
				m.selectedModel++
				if m.selectedModel >= m.modelsScroll+10 {
					m.modelsScroll++
				}
			}
			return m, nil
		case "k", "up":
			if m.selectedModel > 0 {
				m.selectedModel--
				if m.selectedModel < m.modelsScroll {
					m.modelsScroll--
				}
			}
			return m, nil
		}
	}
	return m, nil
}

func (m *DashboardModel) updateOptimizeMode(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case optimizeMsg:
		m.optimizeMessage = msg.message
		m.optimizeSuccess = msg.success
		m.optimizeRestartedModels = msg.restartedModels
		if msg.success {
			m.fetchSequence++
			return m, fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence)
		}
		return m, nil

	case tea.KeyMsg:
		if msg.String() == "esc" {
			m.optimizing = false
			m.optimizeMessage = ""
			m.optimizeSuccess = false
			m.optimizeRestartedModels = nil
			return m, nil
		}
	}
	return m, nil
}

