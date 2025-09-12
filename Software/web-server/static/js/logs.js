// Logs viewer functionality
let ws = null;
let isPaused = false;
let currentService = null;
let logBuffer = [];
let maxLogLines = 2000;
let stats = { lines: 0, errors: 0, warnings: 0 };

async function loadServices() {
    try {
        const response = await fetch('/api/logs/services');
        const data = await response.json();
        const select = document.getElementById('serviceSelect');
        
        select.innerHTML = '<option value="">Select a service...</option>';
        
        data.services.forEach(service => {
            const option = document.createElement('option');
            option.value = service.id;
            option.textContent = service.name;
            option.dataset.status = service.status;
            select.appendChild(option);
        });
        
        const firstRunning = data.services.find(s => s.status === 'running');
        if (firstRunning) {
            select.value = firstRunning.id;
            changeService();
        }
    } catch (error) {
        console.error('Failed to load services:', error);
        const select = document.getElementById('serviceSelect');
        select.innerHTML = '<option value="">Error loading services</option>';
    }
}

function changeService() {
    const select = document.getElementById('serviceSelect');
    const selectedOption = select.options[select.selectedIndex];
    
    if (!select.value) {
        disconnectWebSocket();
        document.getElementById('serviceStatus').style.display = 'none';
        document.getElementById('logViewer').className = 'log-viewer empty';
        return;
    }
    
    currentService = select.value;
    const status = selectedOption.dataset.status;
    
    const statusEl = document.getElementById('serviceStatus');
    statusEl.textContent = status.charAt(0).toUpperCase() + status.slice(1);
    statusEl.className = 'service-status ' + status;
    statusEl.style.display = 'inline-flex';
    
    clearLogs();
    document.getElementById('logViewer').className = 'log-viewer loading';
    
    connectWebSocket(currentService);
}

function connectWebSocket(service) {
    disconnectWebSocket();
    
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/logs`;
    
    ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
        console.log('WebSocket connected');
        updateConnectionStatus(true);
        
        ws.send(JSON.stringify({ service: service }));
        
        const viewer = document.getElementById('logViewer');
        viewer.classList.remove('loading', 'empty');
    };
    
    ws.onmessage = (event) => {
        if (!isPaused) {
            const data = JSON.parse(event.data);
            appendLog(data);
        }
    };
    
    ws.onerror = (error) => {
        console.error('WebSocket error:', error);
        updateConnectionStatus(false);
    };
    
    ws.onclose = () => {
        console.log('WebSocket disconnected');
        updateConnectionStatus(false);
        
        if (currentService) {
            setTimeout(() => {
                if (currentService === service) {
                    connectWebSocket(service);
                }
            }, 3000);
        }
    };
}

function disconnectWebSocket() {
    if (ws) {
        currentService = null;
        ws.close();
        ws = null;
    }
}

function updateConnectionStatus(connected) {
    const indicator = document.getElementById('connectionIndicator');
    const text = document.getElementById('connectionText');
    
    if (connected) {
        indicator.classList.remove('disconnected');
        indicator.classList.add('connected');
        text.textContent = 'Connected';
    } else {
        indicator.classList.remove('connected');
        indicator.classList.add('disconnected');
        text.textContent = 'Disconnected';
    }
}

function appendLog(logData) {
    const viewer = document.getElementById('logViewer');
    const logEntry = document.createElement('div');
    logEntry.className = 'log-entry';
    
    const content = logData.message || logData.content || '';
    if (content.includes('ERROR') || content.includes('[error]')) {
        logEntry.classList.add('error');
        stats.errors++;
    } else if (content.includes('WARN') || content.includes('[warning]')) {
        logEntry.classList.add('warning');
        stats.warnings++;
    } else if (content.includes('INFO') || content.includes('[info]')) {
        logEntry.classList.add('info');
    } else if (content.includes('DEBUG') || content.includes('[debug]')) {
        logEntry.classList.add('debug');
    }
    
    if (logData.timestamp) {
        const timestamp = document.createElement('span');
        timestamp.className = 'log-timestamp';
        
        let dateObj;
        if (typeof logData.timestamp === 'string' && logData.timestamp.length > 10) {
            dateObj = new Date(parseInt(logData.timestamp) / 1000);
        } else if (typeof logData.timestamp === 'number') {
            dateObj = new Date(logData.timestamp);
        } else {
            dateObj = new Date(logData.timestamp);
        }
        
        if (!isNaN(dateObj.getTime())) {
            timestamp.textContent = dateObj.toLocaleTimeString();
        } else {
            timestamp.textContent = '';
        }
        
        logEntry.appendChild(timestamp);
    }
    
    const logContent = document.createElement('span');
    logContent.className = 'log-content';
    logContent.textContent = content;
    logEntry.appendChild(logContent);
    
    viewer.appendChild(logEntry);
    
    logBuffer.push(logEntry);
    if (logBuffer.length > maxLogLines) {
        const oldEntry = logBuffer.shift();
        oldEntry.remove();
    }
    
    stats.lines++;
    updateStats();
    
    if (!isPaused) {
        viewer.scrollTop = viewer.scrollHeight;
    }
}

function updateStats() {
    document.getElementById('lineCount').textContent = stats.lines;
    document.getElementById('errorCount').textContent = stats.errors;
    document.getElementById('warningCount').textContent = stats.warnings;
}

function togglePause() {
    isPaused = !isPaused;
    const button = document.getElementById('pauseButton');
    const btnText = button.querySelector('.btn-text');
    
    if (isPaused) {
        button.classList.add('paused');
        btnText.textContent = 'Resume';
        button.querySelector('svg').innerHTML = '<path d="M8 3.5a5 5 0 0 0-5 5v1a5 5 0 0 0 10 0v-1a5 5 0 0 0-5-5z"/>';
    } else {
        button.classList.remove('paused');
        btnText.textContent = 'Pause';
        button.querySelector('svg').innerHTML = '<path d="M6 3.5a.5.5 0 0 1 .5.5v8a.5.5 0 0 1-1 0V4a.5.5 0 0 1 .5-.5zm4 0a.5.5 0 0 1 .5.5v8a.5.5 0 0 1-1 0V4a.5.5 0 0 1 .5-.5z"/>';
        
        const viewer = document.getElementById('logViewer');
        viewer.scrollTop = viewer.scrollHeight;
    }
}

function clearLogs() {
    const viewer = document.getElementById('logViewer');
    viewer.innerHTML = '';
    logBuffer = [];
    stats = { lines: 0, errors: 0, warnings: 0 };
    updateStats();
}

function downloadLogs() {
    const content = logBuffer.map(entry => {
        const timestamp = entry.querySelector('.log-timestamp')?.textContent || '';
        const logContent = entry.querySelector('.log-content')?.textContent || '';
        return `${timestamp} ${logContent}`;
    }).join('\n');
    
    const blob = new Blob([content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${currentService || 'logs'}_${new Date().toISOString()}.log`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

document.addEventListener('DOMContentLoaded', () => {
    loadServices();
});

window.addEventListener('beforeunload', () => {
    disconnectWebSocket();
});