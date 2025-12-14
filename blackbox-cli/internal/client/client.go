package client

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/maxdcmn/blackbox-cli/internal/model"
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
