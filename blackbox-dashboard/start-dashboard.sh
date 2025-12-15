#!/bin/bash
# Quick start script for Blackbox Dashboard

set -e

echo "================================"
echo "Blackbox Dashboard Quick Start"
echo "================================"
echo ""

# Check if we're in the right directory
if [ ! -f "api.py" ]; then
    echo "Error: Please run this script from the blackbox-dashboard directory"
    exit 1
fi

# Check if uv is installed
if ! command -v uv &> /dev/null; then
    echo "Error: uv is not installed"
    echo "Install it with: curl -LsSf https://astral.sh/uv/install.sh | sh"
    exit 1
fi

# Check if requirements are installed
echo "Checking dependencies..."
if ! python3 -c "import fastapi" 2>/dev/null; then
    echo "Installing dependencies with uv..."
    uv pip install -e .
else
    echo "✓ Dependencies installed"
fi

echo ""
echo "Starting Blackbox Dashboard..."
echo ""
echo "API Server will run on: http://localhost:8001"
echo "Dashboard will be at: http://localhost:8001/"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Start API server in background
uv run python3 api.py &
API_PID=$!

# Wait a bit for API to start
sleep 2

# Start data collector
echo ""
echo "Starting data collector..."
uv run python3 data_collector.py &
COLLECTOR_PID=$!

echo ""
echo "================================"
echo "Dashboard is running!"
echo "================================"
echo ""
echo "Open your browser to: http://localhost:8001/"
echo ""
echo "API PID: $API_PID"
echo "Collector PID: $COLLECTOR_PID"
echo ""
echo "Press Ctrl+C to stop all services"
echo ""

# Trap Ctrl+C to kill both processes
trap "echo 'Stopping services...'; kill $API_PID $COLLECTOR_PID 2>/dev/null; exit" INT TERM

# Wait for both processes
wait