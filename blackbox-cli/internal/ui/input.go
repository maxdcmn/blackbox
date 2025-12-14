package ui

import (
	"strings"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/maxdcmn/blackbox-cli/internal/config"
)

func (m *DashboardModel) renderInputMode(isCreate bool) string {
	var b strings.Builder
	if isCreate {
		b.WriteString("Create New Endpoint\n\n")
	} else {
		b.WriteString("Edit Endpoint\n\n")
	}

	fields := []*string{&m.newName, &m.newURL, &m.newEp, &m.newTO}
	labels := []string{"Name: ", "Base URL: ", "Endpoint: ", "Timeout: "}

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
				return m, fetchSnapshot(m.client, m.timeout, m.selected, m.fetchSequence)
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
