package cmd

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"time"

	"github.com/maxdcmn/blackbox-cli/internal/client"
	"github.com/spf13/cobra"
)

var statFlags struct {
	watch    bool
	interval string
	compact  bool
}

var statCmd = &cobra.Command{
	Use:   "stat",
	Short: "Print a snapshot (JSON) or watch snapshots",
	RunE: func(cmd *cobra.Command, args []string) error {
		timeout, err := time.ParseDuration(rf.timeout)
		if err != nil {
			return fmt.Errorf("invalid --timeout: %w", err)
		}
		interval, err := time.ParseDuration(statFlags.interval)
		if err != nil {
			return fmt.Errorf("invalid --interval: %w", err)
		}

		c := client.New(rf.baseURL, rf.endpoint, timeout)

		printOnce := func() error {
			ctx, cancel := context.WithTimeout(cmd.Context(), timeout)
			defer cancel()

			snap, err := c.Snapshot(ctx)
			if err != nil {
				return err
			}

			enc := json.NewEncoder(os.Stdout)
			if !statFlags.compact {
				enc.SetIndent("", "  ")
			}
			return enc.Encode(snap)
		}

		if !statFlags.watch {
			return printOnce()
		}

		ticker := time.NewTicker(interval)
		defer ticker.Stop()

		for {
			if err := printOnce(); err != nil {
				// keep watch alive; errors are still visible
				fmt.Fprintln(os.Stderr, "error:", err)
			}
			select {
			case <-cmd.Context().Done():
				return nil
			case <-ticker.C:
			}
		}
	},
}

func init() {
	statCmd.Flags().BoolVar(&statFlags.watch, "watch", false, "watch snapshots continuously")
	statCmd.Flags().StringVar(&statFlags.interval, "interval", "3s", "watch interval (e.g. 3s, 1s)")
	statCmd.Flags().BoolVar(&statFlags.compact, "compact", false, "print compact JSON (no indentation)")
}
