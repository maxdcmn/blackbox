package cmd

import (
	"fmt"
	"os"
	"time"

	tea "github.com/charmbracelet/bubbletea"
	"github.com/maxdcmn/blackbox-cli/internal/config"
	"github.com/maxdcmn/blackbox-cli/internal/ui"
	"github.com/spf13/cobra"
)

type rootFlags struct {
	baseURL  string
	endpoint string
	timeout  string
}

var rf rootFlags

var rootCmd = &cobra.Command{
	Use:           "blackbox",
	Short:         "blackbox: CLI monitor for blackbox-server (vLLM KPIs + semantics)",
	SilenceUsage:  true,
	SilenceErrors: true,
	RunE: func(cmd *cobra.Command, args []string) error {
		cfg, err := config.Load()
		if err != nil {
			return fmt.Errorf("failed to load config: %w", err)
		}

		timeout, err := time.ParseDuration(rf.timeout)
		if err != nil {
			timeout = 2 * time.Second
		}
		interval := 1 * time.Second

		m := ui.NewDashboard(cfg, interval, timeout)
		p := tea.NewProgram(m, tea.WithAltScreen())
		if _, err := p.Run(); err != nil {
			return err
		}
		return nil
	},
}

func Execute() {
	if err := rootCmd.Execute(); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func init() {
	rootCmd.PersistentFlags().StringVar(&rf.baseURL, "url", "http://127.0.0.1:8080", "blackbox-server base URL")
	rootCmd.PersistentFlags().StringVar(&rf.endpoint, "endpoint", "/vram", "VRAM endpoint path")
	rootCmd.PersistentFlags().StringVar(&rf.timeout, "timeout", "2s", "HTTP timeout (e.g. 2s, 500ms)")

	rootCmd.AddCommand(statCmd)
}
