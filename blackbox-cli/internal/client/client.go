package client

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/maxdcmn/blackbox-cli/internal/model"
	"github.com/maxdcmn/blackbox-cli/internal/utils"
)

type Client struct {
	baseURL  string
	endpoint string
	http     *http.Client
}

func New(baseURL, endpoint string, timeout time.Duration) *Client {
	return &Client{
		baseURL:  baseURL,
		endpoint: endpoint,
		http: &http.Client{
			Timeout: timeout,
		},
	}
}

func (c *Client) Snapshot(ctx context.Context) (*model.Snapshot, error) {
	fullURL := c.baseURL + c.endpoint

	if strings.HasPrefix(fullURL, "http:/") && !strings.HasPrefix(fullURL, "http://") {
		fullURL = strings.Replace(fullURL, "http:/", "http://", 1)
	}
	if strings.HasPrefix(fullURL, "https:/") && !strings.HasPrefix(fullURL, "https://") {
		fullURL = strings.Replace(fullURL, "https:/", "https://", 1)
	}

	if _, err := url.Parse(fullURL); err != nil {
		return nil, fmt.Errorf("invalid URL %q: %w", fullURL, err)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, fullURL, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("server returned %s", resp.Status)
	}

	var snap model.Snapshot
	if err := json.NewDecoder(resp.Body).Decode(&snap); err != nil {
		if ctx.Err() != nil {
			return nil, fmt.Errorf("request timeout: %w", ctx.Err())
		}
		return nil, fmt.Errorf("failed to decode response: %w", err)
	}

	return &snap, nil
}

func (c *Client) AggregatedSnapshot(ctx context.Context, windowSeconds int) (*model.AggregatedSnapshot, error) {
	baseURL := c.baseURL
	if strings.HasPrefix(baseURL, "http:/") && !strings.HasPrefix(baseURL, "http://") {
		baseURL = strings.Replace(baseURL, "http:/", "http://", 1)
	}
	if strings.HasPrefix(baseURL, "https:/") && !strings.HasPrefix(baseURL, "https://") {
		baseURL = strings.Replace(baseURL, "https:/", "https://", 1)
	}

	aggURL := baseURL + "/vram/aggregated"
	if windowSeconds > 0 {
		aggURL += fmt.Sprintf("?window=%d", windowSeconds)
	}

	if _, err := url.Parse(aggURL); err != nil {
		return nil, fmt.Errorf("invalid URL %q: %w", aggURL, err)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, aggURL, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	// Use a longer timeout for aggregated requests (window + 10 seconds buffer)
	aggClient := &http.Client{
		Timeout: time.Duration(windowSeconds+10) * time.Second,
	}

	resp, err := aggClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("server returned %s", resp.Status)
	}

	var aggSnap model.AggregatedSnapshot
	if err := json.NewDecoder(resp.Body).Decode(&aggSnap); err != nil {
		if ctx.Err() != nil {
			return nil, fmt.Errorf("request timeout: %w", ctx.Err())
		}
		return nil, fmt.Errorf("failed to decode response: %w", err)
	}

	utils.Debug("AggregatedSnapshot received: window=%ds, samples=%d, used_kv_cache_bytes.avg=%.2f, used_kv_cache_bytes.count=%d, models=%d",
		aggSnap.WindowSeconds, aggSnap.SampleCount, aggSnap.UsedKVCacheBytes.Avg, aggSnap.UsedKVCacheBytes.Count, len(aggSnap.Models))
	for i, m := range aggSnap.Models {
		utils.Debug("  Model[%d]: %s, UsedKVCacheBytes=%d, AllocatedVRAMBytes=%d", i, m.ModelID, m.UsedKVCacheBytes, m.AllocatedVRAMBytes)
	}

	return &aggSnap, nil
}

func (c *Client) Stream(ctx context.Context, onSnapshot func(*model.Snapshot) error) error {
	streamURL := c.baseURL + "/vram/stream"
	if strings.HasPrefix(streamURL, "http:/") && !strings.HasPrefix(streamURL, "http://") {
		streamURL = strings.Replace(streamURL, "http:/", "http://", 1)
	}
	if strings.HasPrefix(streamURL, "https:/") && !strings.HasPrefix(streamURL, "https://") {
		streamURL = strings.Replace(streamURL, "https:/", "https://", 1)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, streamURL, nil)
	if err != nil {
		return fmt.Errorf("failed to create request: %w", err)
	}

	// Set SSE-specific headers
	req.Header.Set("Accept", "text/event-stream")
	req.Header.Set("Cache-Control", "no-cache")
	req.Header.Set("Connection", "keep-alive")

	// Create a completely isolated HTTP client for SSE
	// The server sends multiple HTTP responses on the same connection (each SSE event is a full HTTP response)
	// We need to disable connection pooling entirely to prevent "unsolicited response" errors
	transport := &http.Transport{
		DisableKeepAlives:   true, // Disable keep-alive to prevent connection reuse
		MaxIdleConns:        0,    // No connection pooling
		MaxIdleConnsPerHost: 0,    // No per-host pooling
		IdleConnTimeout:     0,    // No timeout
		DisableCompression:  true, // Disable compression for SSE
		// Force new connection for each request
		ForceAttemptHTTP2: false, // Disable HTTP/2 which has different connection handling
	}

	// Create a dedicated client that won't interfere with other requests
	streamClient := &http.Client{
		Timeout:   0, // No timeout for streaming
		Transport: transport,
		// Don't follow redirects
		CheckRedirect: func(req *http.Request, via []*http.Request) error {
			return http.ErrUseLastResponse
		},
	}

	resp, err := streamClient.Do(req)
	if err != nil {
		return fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("server returned %s", resp.Status)
	}

	// Verify content type
	contentType := resp.Header.Get("Content-Type")
	if !strings.Contains(contentType, "text/event-stream") {
		return fmt.Errorf("unexpected content type: %s (expected text/event-stream)", contentType)
	}

	// The server sends each SSE event as a separate HTTP response
	// We need to read the raw stream and parse multiple HTTP responses
	reader := bufio.NewReader(resp.Body)
	var currentData strings.Builder
	skipUntilEmptyLine := false

	for {
		// Check for context cancellation
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}

		// Read line by line
		line, err := reader.ReadString('\n')
		if err != nil {
			if err == io.EOF {
				// Process any remaining data before EOF
				if currentData.Len() > 0 {
					data := currentData.String()
					currentData.Reset()
					var snap model.Snapshot
					if json.Unmarshal([]byte(data), &snap) == nil {
						onSnapshot(&snap)
					}
				}
				return nil
			}
			return fmt.Errorf("stream read error: %w", err)
		}

		line = strings.TrimRight(line, "\r\n")

		// Handle HTTP response headers that appear in the stream
		// The server sends multiple HTTP responses, each starting with headers
		if strings.HasPrefix(line, "HTTP/") {
			// New HTTP response - skip headers until empty line
			skipUntilEmptyLine = true
			currentData.Reset()
			continue
		}

		if skipUntilEmptyLine {
			if line == "" {
				// End of headers, start reading SSE data
				skipUntilEmptyLine = false
			}
			continue
		}

		// Parse SSE format
		if line == "" {
			// Empty line indicates end of SSE event
			if currentData.Len() > 0 {
				data := currentData.String()
				currentData.Reset()

				var snap model.Snapshot
				if err := json.Unmarshal([]byte(data), &snap); err != nil {
					// Skip malformed JSON
					continue
				}

				if err := onSnapshot(&snap); err != nil {
					return err
				}
			}
			continue
		}

		// Handle SSE field lines
		if strings.HasPrefix(line, "data: ") {
			// Extract data after "data: " prefix
			data := strings.TrimSpace(line[6:])
			if data != "" {
				currentData.Reset()
				currentData.WriteString(data)
			}
		} else if strings.HasPrefix(line, ":") {
			// SSE comment - ignore
			continue
		} else if strings.HasPrefix(line, "event:") {
			// SSE metadata - ignore
			continue
		} else if strings.HasPrefix(line, "id:") {
			// SSE metadata - ignore
			continue
		}
		// Ignore any other lines (could be HTTP headers from subsequent responses)
	}
}

type DeployResponse struct {
	Success bool   `json:"success"`
	Message string `json:"message"`
	Port    int    `json:"port,omitempty"`
}

func (c *Client) DeployModel(ctx context.Context, modelID, hfToken, port string) (*DeployResponse, error) {
	baseURL := c.baseURL
	if strings.HasPrefix(baseURL, "http:/") && !strings.HasPrefix(baseURL, "http://") {
		baseURL = strings.Replace(baseURL, "http:/", "http://", 1)
	}
	if strings.HasPrefix(baseURL, "https:/") && !strings.HasPrefix(baseURL, "https://") {
		baseURL = strings.Replace(baseURL, "https:/", "https://", 1)
	}

	deployURL := baseURL + "/deploy"
	if _, err := url.Parse(deployURL); err != nil {
		return nil, fmt.Errorf("invalid URL %q: %w", deployURL, err)
	}

	// Build JSON payload
	payload := map[string]interface{}{
		"model_id": modelID,
	}
	if hfToken != "" {
		payload["hf_token"] = hfToken
	}
	if port != "" {
		payload["port"] = port
	}

	jsonData, err := json.Marshal(payload)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, deployURL, strings.NewReader(string(jsonData)))
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	var deployResp DeployResponse
	if err := json.NewDecoder(resp.Body).Decode(&deployResp); err != nil {
		if ctx.Err() != nil {
			return nil, fmt.Errorf("request timeout: %w", ctx.Err())
		}
		return nil, fmt.Errorf("failed to decode response: %w", err)
	}

	return &deployResp, nil
}

type SpindownResponse struct {
	Success bool   `json:"success"`
	Message string `json:"message"`
	Target  string `json:"target,omitempty"`
}

func (c *Client) SpindownModel(ctx context.Context, modelID, containerID string) (*SpindownResponse, error) {
	baseURL := c.baseURL
	if strings.HasPrefix(baseURL, "http:/") && !strings.HasPrefix(baseURL, "http://") {
		baseURL = strings.Replace(baseURL, "http:/", "http://", 1)
	}
	if strings.HasPrefix(baseURL, "https:/") && !strings.HasPrefix(baseURL, "https://") {
		baseURL = strings.Replace(baseURL, "https:/", "https://", 1)
	}

	spindownURL := baseURL + "/spindown"
	if _, err := url.Parse(spindownURL); err != nil {
		return nil, fmt.Errorf("invalid URL %q: %w", spindownURL, err)
	}

	payload := make(map[string]interface{})
	if modelID != "" {
		payload["model_id"] = modelID
	}
	if containerID != "" {
		payload["container_id"] = containerID
	}

	jsonData, err := json.Marshal(payload)
	if err != nil {
		return nil, fmt.Errorf("failed to marshal request: %w", err)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, spindownURL, strings.NewReader(string(jsonData)))
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}
	req.Header.Set("Content-Type", "application/json")

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	var spindownResp SpindownResponse
	if err := json.NewDecoder(resp.Body).Decode(&spindownResp); err != nil {
		if ctx.Err() != nil {
			return nil, fmt.Errorf("request timeout: %w", ctx.Err())
		}
		return nil, fmt.Errorf("failed to decode response: %w", err)
	}

	return &spindownResp, nil
}

type ModelsResponse struct {
	Total      int             `json:"total"`
	Running    int             `json:"running"`
	MaxAllowed int             `json:"max_allowed"`
	Models     []DeployedModel `json:"models"`
}

type DeployedModel struct {
	ModelID                     string  `json:"model_id"`
	ContainerID                 string  `json:"container_id"`
	ContainerName               string  `json:"container_name"`
	Port                        int     `json:"port"`
	Running                     bool    `json:"running"`
	ConfiguredMaxGPUUtilization float64 `json:"configured_max_gpu_utilization"`
	AvgVRAMUsagePercent         float64 `json:"avg_vram_usage_percent"`
	PeakVRAMUsagePercent        float64 `json:"peak_vram_usage_percent"`
	GPUType                     string  `json:"gpu_type"`
	PID                         int     `json:"pid"`
}

func (c *Client) ListModels(ctx context.Context) (*ModelsResponse, error) {
	baseURL := c.baseURL
	if strings.HasPrefix(baseURL, "http:/") && !strings.HasPrefix(baseURL, "http://") {
		baseURL = strings.Replace(baseURL, "http:/", "http://", 1)
	}
	if strings.HasPrefix(baseURL, "https:/") && !strings.HasPrefix(baseURL, "https://") {
		baseURL = strings.Replace(baseURL, "https:/", "https://", 1)
	}

	modelsURL := baseURL + "/models"
	if _, err := url.Parse(modelsURL); err != nil {
		return nil, fmt.Errorf("invalid URL %q: %w", modelsURL, err)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, modelsURL, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("server returned %s", resp.Status)
	}

	var modelsResp ModelsResponse
	if err := json.NewDecoder(resp.Body).Decode(&modelsResp); err != nil {
		if ctx.Err() != nil {
			return nil, fmt.Errorf("request timeout: %w", ctx.Err())
		}
		return nil, fmt.Errorf("failed to decode response: %w", err)
	}

	return &modelsResp, nil
}

type OptimizeResponse struct {
	Success         bool     `json:"success"`
	Optimized       bool     `json:"optimized"`
	Message         string   `json:"message"`
	RestartedModels []string `json:"restarted_models,omitempty"`
}

func (c *Client) Optimize(ctx context.Context) (*OptimizeResponse, error) {
	baseURL := c.baseURL
	if strings.HasPrefix(baseURL, "http:/") && !strings.HasPrefix(baseURL, "http://") {
		baseURL = strings.Replace(baseURL, "http:/", "http://", 1)
	}
	if strings.HasPrefix(baseURL, "https:/") && !strings.HasPrefix(baseURL, "https://") {
		baseURL = strings.Replace(baseURL, "https:/", "https://", 1)
	}

	optimizeURL := baseURL + "/optimize"
	if _, err := url.Parse(optimizeURL); err != nil {
		return nil, fmt.Errorf("invalid URL %q: %w", optimizeURL, err)
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, optimizeURL, nil)
	if err != nil {
		return nil, fmt.Errorf("failed to create request: %w", err)
	}

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	var optimizeResp OptimizeResponse
	if err := json.NewDecoder(resp.Body).Decode(&optimizeResp); err != nil {
		if ctx.Err() != nil {
			return nil, fmt.Errorf("request timeout: %w", ctx.Err())
		}
		return nil, fmt.Errorf("failed to decode response: %w", err)
	}

	return &optimizeResp, nil
}
