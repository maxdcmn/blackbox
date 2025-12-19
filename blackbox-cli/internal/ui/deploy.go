package ui

import (
	"context"
	"fmt"
	"strings"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/maxdcmn/blackbox-cli/internal/client"
)

func (m *DashboardModel) renderDeployMode() string {
	var b strings.Builder
	b.WriteString("Deploy Model\n\n")

	fields := []*string{&m.deployModelID, &m.deployHFToken, &m.deployPort}
	labels := []string{"Model ID: ", "HF Token (optional): ", "Port (optional): "}

	maxLabelWidth := 0
	for _, label := range labels {
		if len(label) > maxLabelWidth {
			maxLabelWidth = len(label)
		}
	}

	for i, field := range fields {
		fieldValue := *field
		cursorPos := m.cursorPos[i]

		var fieldContent string
		if i == m.inputField {
			if cursorPos >= 0 && cursorPos <= len(fieldValue) {
				fieldContent = fieldValue[:cursorPos] + "█" + fieldValue[cursorPos:]
			} else {
				fieldContent = fieldValue + "█"
			}
		} else {
			fieldContent = fieldValue
		}

		labelText := labels[i]
		paddedLabel := labelText + strings.Repeat(" ", maxLabelWidth-len(labelText))

		if i == m.inputField {
			labelPart := fieldStyle.Render(paddedLabel)
			contentPart := activeFieldStyle.Render(fieldContent)
			b.WriteString(labelPart + contentPart)
		} else {
			labelPart := fieldStyle.Render(paddedLabel)
			contentPart := fieldContent
			b.WriteString(labelPart + contentPart)
		}
		b.WriteString("\n")
	}

	if m.deployMessage != "" {
		b.WriteString("\n")
		if m.deploySuccess {
			b.WriteString(styleColor(colorGreen).Render("✓ " + m.deployMessage))
		} else {
			b.WriteString(styleColor(colorRed).Render("✗ " + m.deployMessage))
		}
		b.WriteString("\n")
	}

	b.WriteString("\nTab: next field  Enter: deploy  Esc: cancel")
	return popupStyle.Width(70).Render(b.String())
}

type deployMsg struct {
	success bool
	message string
	port    int
}

func deployModel(c *client.Client, timeout time.Duration, modelID, hfToken, port string) tea.Cmd {
	return func() tea.Msg {
		// Use short timeout - just enough to send request and get initial response
		shortTimeout := 3 * time.Second
		if shortTimeout > timeout {
			shortTimeout = timeout
		}
		ctx, cancel := context.WithTimeout(context.Background(), shortTimeout)
		defer cancel()

		resp, err := c.DeployModel(ctx, modelID, hfToken, port)
		if err != nil {
			// If timeout or network error, assume deployment started
			if ctx.Err() == context.DeadlineExceeded {
				return deployMsg{success: true, message: "I hope it's being deployed! (request sent, check status with 'm')"}
			}
			return deployMsg{success: false, message: err.Error()}
		}

		msg := "Deployment " + resp.Message
		if resp.Port > 0 {
			msg += fmt.Sprintf(" (port: %d)", resp.Port)
		}
		return deployMsg{success: resp.Success, message: msg, port: resp.Port}
	}
}

func (m *DashboardModel) updateDeployMode(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case deployMsg:
		m.deployMessage = msg.message
		m.deploySuccess = msg.success
		if msg.success {
			// Refresh data after successful deploy
			m.fetchSequence++
			return m, fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence)
		}
		return m, nil

	case tea.KeyMsg:
		switch msg.String() {
		case "esc":
			m.deploying = false
			m.deployMessage = ""
			m.deploySuccess = false
			return m, nil
		case "enter":
			if m.deployModelID == "" {
				return m, nil
			}
			// Deploy the model
			ep := m.endpoints[m.selected]
			deployClient := client.New(ep.BaseURL, ep.Endpoint, m.timeout)
			return m, deployModel(deployClient, m.timeout, m.deployModelID, m.deployHFToken, m.deployPort)
		case "tab":
			m.ensureDeployCursorInBounds()
			m.inputField = (m.inputField + 1) % 3
			m.ensureDeployCursorInBounds()
			return m, nil
		case "left":
			pos := &m.cursorPos[m.inputField]
			if *pos > 0 {
				*pos--
			}
			return m, nil
		case "right":
			field := m.getDeployFieldValue()
			pos := &m.cursorPos[m.inputField]
			if field != nil && *pos < len(*field) {
				*pos++
			}
			return m, nil
		case "home":
			m.cursorPos[m.inputField] = 0
			return m, nil
		case "end":
			field := m.getDeployFieldValue()
			if field != nil {
				m.cursorPos[m.inputField] = len(*field)
			}
			return m, nil
		case "backspace":
			field := m.getDeployFieldValue()
			pos := &m.cursorPos[m.inputField]
			if field != nil && *pos > 0 {
				*field = (*field)[:*pos-1] + (*field)[*pos:]
				*pos--
			}
			return m, nil
		case "delete":
			field := m.getDeployFieldValue()
			pos := &m.cursorPos[m.inputField]
			if field != nil && *pos < len(*field) {
				*field = (*field)[:*pos] + (*field)[*pos+1:]
			}
			return m, nil
		default:
			if msg.Type == tea.KeyRunes {
				field := m.getDeployFieldValue()
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

func (m *DashboardModel) getDeployFieldValue() *string {
	fields := []*string{&m.deployModelID, &m.deployHFToken, &m.deployPort}
	if m.inputField >= 0 && m.inputField < len(fields) {
		return fields[m.inputField]
	}
	return nil
}

func (m *DashboardModel) ensureDeployCursorInBounds() {
	fields := []*string{&m.deployModelID, &m.deployHFToken, &m.deployPort}
	if m.inputField >= 0 && m.inputField < len(fields) {
		fieldLen := len(*fields[m.inputField])
		if m.cursorPos[m.inputField] < 0 {
			m.cursorPos[m.inputField] = 0
		} else if m.cursorPos[m.inputField] > fieldLen {
			m.cursorPos[m.inputField] = fieldLen
		}
	}
}

