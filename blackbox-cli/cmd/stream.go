package cmd

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"strings"

	"github.com/maxdcmn/blackbox-cli/internal/model"
	"github.com/spf13/cobra"
)

var streamCmd = &cobra.Command{
	Use:   "stream",
	Short: "Stream real-time VRAM metrics via SSE",
	RunE: func(cmd *cobra.Command, args []string) error {
		streamURL := rf.baseURL + "/vram/stream"
		if strings.HasPrefix(streamURL, "http:/") && !strings.HasPrefix(streamURL, "http://") {
			streamURL = strings.Replace(streamURL, "http:/", "http://", 1)
		}
		if strings.HasPrefix(streamURL, "https:/") && !strings.HasPrefix(streamURL, "https://") {
			streamURL = strings.Replace(streamURL, "https:/", "https://", 1)
		}

		ctx, cancel := context.WithCancel(cmd.Context())
		defer cancel()

		req, err := http.NewRequestWithContext(ctx, http.MethodGet, streamURL, nil)
		if err != nil {
			return fmt.Errorf("failed to create request: %w", err)
		}

		client := &http.Client{Timeout: 0}
		resp, err := client.Do(req)
		if err != nil {
			return fmt.Errorf("request failed: %w", err)
		}
		defer resp.Body.Close()

		if resp.StatusCode < 200 || resp.StatusCode >= 300 {
			return fmt.Errorf("server returned %s", resp.Status)
		}

		scanner := bufio.NewScanner(resp.Body)
		enc := json.NewEncoder(os.Stdout)
		if !streamFlags.compact {
			enc.SetIndent("", "  ")
		}

		for scanner.Scan() {
			line := scanner.Text()
			if strings.HasPrefix(line, "data: ") {
				data := line[6:]
				var snap model.Snapshot
				if err := json.Unmarshal([]byte(data), &snap); err == nil {
					if err := enc.Encode(snap); err != nil {
						fmt.Fprintf(os.Stderr, "error encoding: %v\n", err)
					}
				}
			}
		}

		if err := scanner.Err(); err != nil && err != io.EOF {
			return fmt.Errorf("stream error: %w", err)
		}

		return nil
	},
}

var streamFlags struct {
	compact bool
}

func init() {
	streamCmd.Flags().BoolVar(&streamFlags.compact, "compact", false, "print compact JSON (no indentation)")
	rootCmd.AddCommand(streamCmd)
}

