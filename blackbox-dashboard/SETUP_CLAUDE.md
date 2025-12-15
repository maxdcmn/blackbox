# Claude AI Chat Setup

## Overview
The dashboard now includes an AI-powered chat assistant that can analyze your GPU metrics and answer questions about KV-cache utilization, memory usage, and performance.

## Setup Instructions

### 1. Install Dependencies
```bash
pip install -r requirements.txt
```

This will install:
- `anthropic` - Claude API client
- `python-dotenv` - Environment variable management

### 2. Get Claude API Key
1. Visit [Anthropic Console](https://console.anthropic.com/)
2. Sign up or log in
3. Go to API Keys section
4. Create a new API key

### 3. Configure Environment
Edit the `.env` file and add your API key:

```bash
ANTHROPIC_API_KEY=sk-ant-api03-your-key-here
```

### 4. Restart the API Server
```bash
# Press Ctrl+C to stop the current server
python3 api.py
```

## Features

### Chat Interface
- **Location**: Fixed panel at bottom-right of the dashboard
- **Minimize/Expand**: Click the header to minimize or expand
- **Node Selection**: Choose specific nodes to analyze or "All Nodes"

### Suggested Questions
Quick-start buttons for common queries:
- **KV-cache analysis**: Analyzes current utilization patterns
- **Get recommendations**: AI suggestions for optimization
- **Performance check**: Identifies potential issues
- **Memory status**: Explains memory fragmentation

### Custom Questions
Ask anything about your GPU metrics:
- "What does my KV-cache utilization tell me?"
- "Are there any performance issues?"
- "Why is memory fragmentation high?"
- "How can I improve utilization?"
- "Compare nodes and tell me which performs better"
- "What are the trends over the last hour?"

### Context Provided to AI
The assistant receives:
- Current metrics (KV-cache, memory, fragmentation)
- Last hour statistics (avg, min, max)
- Number of processes and blocks
- Utilization percentages
- Historical trends

## Usage Examples

### Analyze All Nodes
1. Keep "All Nodes" selected
2. Ask: "What are your recommendations?"
3. AI analyzes all nodes and provides insights

### Analyze Specific Node
1. Uncheck "All Nodes"
2. Select specific node(s) from the list
3. Ask: "Is this node performing well?"
4. AI analyzes only selected nodes

### Compare Nodes
1. Select multiple nodes
2. Ask: "Compare these nodes"
3. AI provides comparative analysis

## Troubleshooting

### "Claude API not available" Error
- Check that `ANTHROPIC_API_KEY` is set in `.env`
- Verify the API key is valid
- Restart the API server after adding the key

### No Response
- Check API server logs for errors
- Verify you have API credits in Anthropic Console
- Check network connectivity

### Empty Metrics
- Ensure nodes are enabled and tracking
- Wait for data to be collected (at least 5 seconds)
- Check that the blackbox VRAM monitor is running on GPU servers

## API Endpoint

The chat functionality uses:
```
POST /api/chat
{
  "message": "Your question here",
  "node_ids": [1, 2] // or null for all nodes
}
```

Response:
```
{
  "response": "AI analysis here",
  "available": true
}
```

## Costs

Claude API pricing (as of 2025):
- Claude Sonnet 4: $3/million input tokens, $15/million output tokens
- Typical query: ~500-1000 input tokens, ~200-400 output tokens
- Cost per query: ~$0.01-0.02

Monitor usage in [Anthropic Console](https://console.anthropic.com/)

## Privacy

- Metrics data is sent to Anthropic's API
- No data is stored by Anthropic beyond processing
- All data is your GPU monitoring metrics only
- See [Anthropic's Privacy Policy](https://www.anthropic.com/privacy)
