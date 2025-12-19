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

var modelsCmd = &cobra.Command{
	Use:   "models",
	Short: "List all deployed models",
	RunE: func(cmd *cobra.Command, args []string) error {
		timeout, err := time.ParseDuration(rf.timeout)
		if err != nil {
			return fmt.Errorf("invalid --timeout: %w", err)
		}

		c := client.New(rf.baseURL, rf.endpoint, timeout)
		ctx, cancel := context.WithTimeout(cmd.Context(), timeout)
		defer cancel()

		models, err := c.ListModels(ctx)
		if err != nil {
			return err
		}

		enc := json.NewEncoder(os.Stdout)
		enc.SetIndent("", "  ")
		return enc.Encode(models)
	},
}

var spindownCmd = &cobra.Command{
	Use:   "spindown",
	Short: "Stop and remove a deployed model",
	Args:  cobra.ExactArgs(1),
	RunE: func(cmd *cobra.Command, args []string) error {
		timeout, err := time.ParseDuration(rf.timeout)
		if err != nil {
			return fmt.Errorf("invalid --timeout: %w", err)
		}

		c := client.New(rf.baseURL, rf.endpoint, timeout)
		ctx, cancel := context.WithTimeout(cmd.Context(), timeout)
		defer cancel()

		modelID := args[0]
		resp, err := c.SpindownModel(ctx, modelID, "")
		if err != nil {
			return err
		}

		if resp.Success {
			fmt.Println("✓", resp.Message)
		} else {
			fmt.Fprintln(os.Stderr, "✗", resp.Message)
			os.Exit(1)
		}
		return nil
	},
}

var optimizeCmd = &cobra.Command{
	Use:   "optimize",
	Short: "Optimize GPU utilization by restarting overallocated models",
	RunE: func(cmd *cobra.Command, args []string) error {
		timeout, err := time.ParseDuration(rf.timeout)
		if err != nil {
			return fmt.Errorf("invalid --timeout: %w", err)
		}

		c := client.New(rf.baseURL, rf.endpoint, timeout)
		ctx, cancel := context.WithTimeout(cmd.Context(), timeout*5)
		defer cancel()

		resp, err := c.Optimize(ctx)
		if err != nil {
			return err
		}

		if resp.Success {
			fmt.Println("✓", resp.Message)
			if resp.Optimized && len(resp.RestartedModels) > 0 {
				fmt.Println("Restarted models:")
				for _, model := range resp.RestartedModels {
					fmt.Println("  -", model)
				}
			}
		} else {
			fmt.Fprintln(os.Stderr, "✗", resp.Message)
			os.Exit(1)
		}
		return nil
	},
}

func init() {
	rootCmd.AddCommand(modelsCmd)
	rootCmd.AddCommand(spindownCmd)
	rootCmd.AddCommand(optimizeCmd)
}




