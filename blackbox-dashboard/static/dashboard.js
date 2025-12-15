// Configuration
const API_BASE_URL = 'http://localhost:8001/api';

// State
let charts = {};
let refreshIntervalId = null;
let currentNodeId = null;  // null = all nodes
let availableNodes = [];

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    initializeCharts();
    loadNodes();
    loadData();
    setupEventListeners();
});

// Event Listeners
function setupEventListeners() {
    document.getElementById('timeRange').addEventListener('change', loadData);
    document.getElementById('refreshInterval').addEventListener('change', (e) => {
        const interval = parseInt(e.target.value);
        if (refreshIntervalId) {
            clearInterval(refreshIntervalId);
            refreshIntervalId = null;
        }
        if (interval > 0) {
            refreshIntervalId = setInterval(loadData, interval);
        }
    });

    // Node selector
    document.getElementById('nodeSelector')?.addEventListener('change', (e) => {
        currentNodeId = e.target.value === 'all' ? null : parseInt(e.target.value);
        loadData();
    });

    // Start auto-refresh
    const defaultInterval = parseInt(document.getElementById('refreshInterval').value);
    if (defaultInterval > 0) {
        refreshIntervalId = setInterval(loadData, defaultInterval);
    }
}

// Initialize Charts
function initializeCharts() {
    const commonOptions = {
        responsive: true,
        maintainAspectRatio: false,
        plugins: {
            legend: {
                display: false
            }
        },
        scales: {
            x: {
                type: 'time',
                time: {
                    displayFormats: {
                        minute: 'HH:mm',
                        hour: 'HH:mm'
                    }
                },
                grid: {
                    display: false
                }
            },
            y: {
                beginAtZero: true,
                grid: {
                    color: 'rgba(0,0,0,0.05)'
                }
            }
        }
    };

    // Memory Usage Chart
    charts.memory = new Chart(
        document.getElementById('memoryChart'),
        {
            type: 'line',
            data: {
                datasets: [{
                    label: 'Memory Used (GB)',
                    data: [],
                    borderColor: 'rgb(102, 126, 234)',
                    backgroundColor: 'rgba(102, 126, 234, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                ...commonOptions,
                scales: {
                    ...commonOptions.scales,
                    y: {
                        ...commonOptions.scales.y,
                        ticks: {
                            callback: function(value) {
                                return value.toFixed(2) + ' GB';
                            }
                        }
                    }
                }
            }
        }
    );

    // Percentage Chart
    charts.percent = new Chart(
        document.getElementById('percentChart'),
        {
            type: 'line',
            data: {
                datasets: [{
                    label: 'Usage %',
                    data: [],
                    borderColor: 'rgb(16, 185, 129)',
                    backgroundColor: 'rgba(16, 185, 129, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                ...commonOptions,
                scales: {
                    ...commonOptions.scales,
                    y: {
                        ...commonOptions.scales.y,
                        max: 100,
                        ticks: {
                            callback: function(value) {
                                return value + '%';
                            }
                        }
                    }
                }
            }
        }
    );

    // Process Count Chart
    charts.process = new Chart(
        document.getElementById('processChart'),
        {
            type: 'line',
            data: {
                datasets: [{
                    label: 'Active Processes',
                    data: [],
                    borderColor: 'rgb(245, 158, 11)',
                    backgroundColor: 'rgba(245, 158, 11, 0.1)',
                    fill: true,
                    tension: 0.4,
                    stepped: true
                }]
            },
            options: {
                ...commonOptions,
                scales: {
                    ...commonOptions.scales,
                    y: {
                        ...commonOptions.scales.y,
                        ticks: {
                            stepSize: 1
                        }
                    }
                }
            }
        }
    );

    // Fragmentation Chart
    charts.fragmentation = new Chart(
        document.getElementById('fragmentationChart'),
        {
            type: 'line',
            data: {
                datasets: [{
                    label: 'Fragmentation Ratio',
                    data: [],
                    borderColor: 'rgb(239, 68, 68)',
                    backgroundColor: 'rgba(239, 68, 68, 0.1)',
                    fill: true,
                    tension: 0.4
                }]
            },
            options: {
                ...commonOptions,
                scales: {
                    ...commonOptions.scales,
                    y: {
                        ...commonOptions.scales.y,
                        ticks: {
                            callback: function(value) {
                                return value.toFixed(4);
                            }
                        }
                    }
                }
            }
        }
    );
}

// Load All Data
async function loadData() {
    try {
        setStatus(true, 'Loading...');
        clearError();

        const duration = parseInt(document.getElementById('timeRange').value);

        // Load latest snapshot for stats
        await loadLatestStats();

        // Load timeseries data
        await Promise.all([
            loadTimeseries('used_bytes', duration, charts.memory, (v) => v / (1024**3)), // Convert to GB
            loadTimeseries('used_percent', duration, charts.percent),
            loadTimeseries('num_processes', duration, charts.process),
            loadTimeseries('fragmentation_ratio', duration, charts.fragmentation)
        ]);

        // Load process list
        await loadProcesses();

        setStatus(true, 'Connected');
    } catch (error) {
        console.error('Error loading data:', error);
        setStatus(false, 'Error');
        showError(`Failed to load data: ${error.message}`);
    }
}

// Load Nodes
async function loadNodes() {
    try {
        const response = await fetch(`${API_BASE_URL}/nodes`);
        if (!response.ok) return;

        availableNodes = await response.json();

        // Update node selector
        const selector = document.getElementById('nodeSelector');
        if (selector) {
            selector.innerHTML = '<option value="all">All Nodes</option>' +
                availableNodes.map(node =>
                    `<option value="${node.id}">${escapeHtml(node.name)}</option>`
                ).join('');
        }
    } catch (error) {
        console.error('Error loading nodes:', error);
    }
}

// Load Latest Stats
async function loadLatestStats() {
    const url = currentNodeId
        ? `${API_BASE_URL}/snapshots/latest?node_id=${currentNodeId}`
        : `${API_BASE_URL}/snapshots/latest`;

    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }

    const data = await response.json();

    // Update stat cards
    document.getElementById('memoryUsed').innerHTML =
        (data.used_bytes / (1024**3)).toFixed(2) + '<span class="stat-unit">GB</span>';
    document.getElementById('memoryPercent').innerHTML =
        data.used_percent.toFixed(1) + '<span class="stat-unit">%</span>';
    document.getElementById('processCount').innerHTML = data.num_processes;
    document.getElementById('fragmentation').innerHTML = data.fragmentation_ratio.toFixed(4);
}

// Load Timeseries Data
async function loadTimeseries(metric, duration, chart, transform = null) {
    let url = `${API_BASE_URL}/timeseries/${metric}?duration=${duration}`;
    if (currentNodeId) {
        url += `&node_id=${currentNodeId}`;
    }

    const response = await fetch(url);

    if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }

    const data = await response.json();

    // Transform data for Chart.js
    const chartData = data.map(point => ({
        x: new Date(point.timestamp),
        y: transform ? transform(point.value) : point.value
    }));

    // Update chart
    chart.data.datasets[0].data = chartData;
    chart.update('none'); // Update without animation for smooth refresh
}

// Load Process List
async function loadProcesses() {
    const duration = parseInt(document.getElementById('timeRange').value);
    let url = `${API_BASE_URL}/processes?duration=${duration}`;
    if (currentNodeId) {
        url += `&node_id=${currentNodeId}`;
    }

    const response = await fetch(url);

    if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }

    const processes = await response.json();

    const processList = document.getElementById('processList');

    if (processes.length === 0) {
        processList.innerHTML = '<div class="loading">No active processes</div>';
        return;
    }

    // Get latest snapshot for each process
    const processItems = processes.map(proc => {
        const latest = proc.history[proc.history.length - 1];
        return {
            pid: proc.pid,
            name: proc.name,
            used_bytes: latest.used_bytes,
            reserved_bytes: latest.reserved_bytes
        };
    });

    // Sort by memory usage
    processItems.sort((a, b) => b.used_bytes - a.used_bytes);

    // Render process list
    processList.innerHTML = processItems.map(proc => `
        <div class="process-item">
            <div class="process-info">
                <div class="process-name">${escapeHtml(proc.name)}</div>
                <div class="process-pid">PID: ${proc.pid}</div>
            </div>
            <div class="process-memory">
                ${formatBytes(proc.used_bytes)}
            </div>
        </div>
    `).join('');
}

// Helper Functions
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function setStatus(connected, text) {
    const indicator = document.getElementById('statusIndicator');
    const statusText = document.getElementById('statusText');

    if (connected) {
        indicator.classList.remove('error');
    } else {
        indicator.classList.add('error');
    }

    statusText.textContent = text;
}

function showError(message) {
    const errorContainer = document.getElementById('errorContainer');
    errorContainer.innerHTML = `<div class="error">⚠️ ${escapeHtml(message)}</div>`;
}

function clearError() {
    document.getElementById('errorContainer').innerHTML = '';
}

function refreshData() {
    loadData();
}

// Node Management Functions
function openNodeManager() {
    document.getElementById('nodeModal').style.display = 'block';
    refreshNodeList();
}

function closeNodeManager() {
    document.getElementById('nodeModal').style.display = 'none';
}

async function refreshNodeList() {
    try {
        const response = await fetch(`${API_BASE_URL}/nodes`);
        if (!response.ok) throw new Error('Failed to load nodes');

        const nodes = await response.json();
        const tbody = document.getElementById('nodeTableBody');

        if (nodes.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" style="text-align:center; color:#888;">No nodes configured</td></tr>';
            return;
        }

        tbody.innerHTML = nodes.map(node => {
            const lastSeen = node.last_seen
                ? new Date(node.last_seen).toLocaleString()
                : 'Never';
            const statusClass = node.enabled ? 'active' : 'inactive';
            const statusText = node.enabled ? 'Enabled' : 'Disabled';
            const toggleText = node.enabled ? 'Disable' : 'Enable';

            return `
                <tr>
                    <td><strong>${escapeHtml(node.name)}</strong></td>
                    <td>${escapeHtml(node.host)}</td>
                    <td>${node.port}</td>
                    <td><span class="node-status ${statusClass}">${statusText}</span></td>
                    <td>${lastSeen}</td>
                    <td class="node-actions">
                        <button class="toggle" onclick="toggleNode(${node.id}, ${!node.enabled})">${toggleText}</button>
                        <button class="delete" onclick="deleteNode(${node.id})">Delete</button>
                    </td>
                </tr>
            `;
        }).join('');
    } catch (error) {
        console.error('Error refreshing node list:', error);
        document.getElementById('nodeTableBody').innerHTML =
            '<tr><td colspan="6" style="text-align:center; color:#dc2626;">Error loading nodes</td></tr>';
    }
}

async function addNode() {
    const name = document.getElementById('nodeName').value.trim();
    const host = document.getElementById('nodeHost').value.trim();
    const port = parseInt(document.getElementById('nodePort').value);

    if (!name || !host || !port) {
        alert('Please fill in all fields');
        return;
    }

    try {
        const response = await fetch(`${API_BASE_URL}/nodes`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ name, host, port })
        });

        if (!response.ok) {
            const error = await response.json();
            throw new Error(error.detail || 'Failed to add node');
        }

        // Clear form
        document.getElementById('nodeName').value = '';
        document.getElementById('nodeHost').value = '';
        document.getElementById('nodePort').value = '6767';

        // Refresh both lists
        await refreshNodeList();
        await loadNodes();

        alert(`Node "${name}" added successfully!`);
    } catch (error) {
        alert(`Error adding node: ${error.message}`);
    }
}

async function deleteNode(nodeId) {
    if (!confirm('Are you sure you want to delete this node? All its data will be removed.')) {
        return;
    }

    try {
        const response = await fetch(`${API_BASE_URL}/nodes/${nodeId}`, {
            method: 'DELETE'
        });

        if (!response.ok) throw new Error('Failed to delete node');

        // Refresh both lists
        await refreshNodeList();
        await loadNodes();

        // Reset node selector if the deleted node was selected
        const selector = document.getElementById('nodeSelector');
        if (currentNodeId === nodeId) {
            currentNodeId = null;
            selector.value = 'all';
            loadData();
        }
    } catch (error) {
        alert(`Error deleting node: ${error.message}`);
    }
}

async function toggleNode(nodeId, enabled) {
    try {
        const response = await fetch(`${API_BASE_URL}/nodes/${nodeId}?enabled=${enabled}`, {
            method: 'PUT'
        });

        if (!response.ok) throw new Error('Failed to update node');

        await refreshNodeList();
    } catch (error) {
        alert(`Error toggling node: ${error.message}`);
    }
}

// Close modal when clicking outside
window.onclick = function(event) {
    const modal = document.getElementById('nodeModal');
    if (event.target === modal) {
        closeNodeManager();
    }
}