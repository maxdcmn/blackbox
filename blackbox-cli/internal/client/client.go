package client

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
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
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, c.baseURL+c.endpoint, nil)
	if err != nil {
		return nil, err
	}

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("server returned %s", resp.Status)
	}

	var snap model.Snapshot
	if err := json.NewDecoder(resp.Body).Decode(&snap); err != nil {
		return nil, err
	}

	return &snap, nil
}
