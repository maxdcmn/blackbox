package config

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

type Endpoint struct {
	Name     string `json:"name"`
	BaseURL  string `json:"base_url"`
	Endpoint string `json:"endpoint"`
	Timeout  string `json:"timeout"`
}

type Config struct {
	Endpoints []Endpoint `json:"endpoints"`
}

var configPath string

func init() {
	home, err := os.UserHomeDir()
	if err != nil {
		home = "."
	}
	configPath = filepath.Join(home, ".config", "blackbox", "config.json")
}

func Load() (*Config, error) {
	data, err := os.ReadFile(configPath)
	if os.IsNotExist(err) {
		return &Config{
			Endpoints: []Endpoint{
				{
					Name:     "local",
					BaseURL:  "http://127.0.0.1:8080",
					Endpoint: "/vram",
					Timeout:  "2s",
				},
			},
		}, nil
	}
	if err != nil {
		return nil, fmt.Errorf("failed to read config: %w", err)
	}

	var cfg Config
	if err := json.Unmarshal(data, &cfg); err != nil {
		return nil, fmt.Errorf("failed to parse config: %w", err)
	}

	if len(cfg.Endpoints) == 0 {
		cfg.Endpoints = []Endpoint{
			{
				Name:     "local",
				BaseURL:  "http://127.0.0.1:8080",
				Endpoint: "/vram",
				Timeout:  "2s",
			},
		}
	}

	return &cfg, nil
}

func Save(cfg *Config) error {
	if err := os.MkdirAll(filepath.Dir(configPath), 0755); err != nil {
		return fmt.Errorf("failed to create config directory: %w", err)
	}

	data, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return fmt.Errorf("failed to marshal config: %w", err)
	}

	if err := os.WriteFile(configPath, data, 0644); err != nil {
		return fmt.Errorf("failed to write config: %w", err)
	}

	return nil
}

func AddEndpoint(cfg *Config, ep Endpoint) error {
	for _, e := range cfg.Endpoints {
		if e.Name == ep.Name {
			return fmt.Errorf("endpoint with name '%s' already exists", ep.Name)
		}
	}
	cfg.Endpoints = append(cfg.Endpoints, ep)
	return Save(cfg)
}

func RemoveEndpoint(cfg *Config, name string) error {
	found := false
	endpoints := make([]Endpoint, 0, len(cfg.Endpoints))
	for _, e := range cfg.Endpoints {
		if e.Name != name {
			endpoints = append(endpoints, e)
		} else {
			found = true
		}
	}
	if !found {
		return fmt.Errorf("endpoint '%s' not found", name)
	}
	cfg.Endpoints = endpoints
	return Save(cfg)
}

func UpdateEndpoint(cfg *Config, oldName string, newEp Endpoint) error {
	for i, e := range cfg.Endpoints {
		if e.Name == oldName {
			cfg.Endpoints[i] = newEp
			return Save(cfg)
		}
	}
	return fmt.Errorf("endpoint '%s' not found", oldName)
}
