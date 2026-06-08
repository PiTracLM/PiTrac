// Logs viewer functionality
let ws = null;
let isPaused = false;
let currentService = null;
let logBuffer = [];
const maxLogLines = 2000;
// Hard ceiling for scrollback growth; trims from the newest end so live-tail trimming stays unaffected
const maxTotalLines = 5000;
let stats = { lines: 0, errors: 0, warnings: 0 };

// Scrollback state — reset on every anchor message or service switch
let currentAnchor = null;
let oldestCursor = null;
let reachedStart = false;
let historyLoading = false;

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

    resetScrollbackState(null);
    clearLogs();
    document.getElementById('logViewer').className = 'log-viewer loading';

    connectWebSocket(currentService);
}

function resetScrollbackState(anchor) {
    currentAnchor = anchor;
    oldestCursor = anchor ? { file: anchor.file, offset: anchor.offset } : null;
    reachedStart = false;
    historyLoading = false;
}

// Shared line→div renderer used by both appendLog and history prepend
function makeLogEntry(content) {
    const logEntry = document.createElement('div');
    logEntry.className = 'log-entry';

    if (content.includes('ERROR') || content.includes('[error]')) {
        logEntry.classList.add('error');
    } else if (content.includes('WARN') || content.includes('[warning]')) {
        logEntry.classList.add('warning');
    } else if (content.includes('INFO') || content.includes('[info]')) {
        logEntry.classList.add('info');
    } else if (content.includes('DEBUG') || content.includes('[debug]')) {
        logEntry.classList.add('debug');
    }

    const logContent = document.createElement('span');
    logContent.className = 'log-content';
    logContent.textContent = content;
    logEntry.appendChild(logContent);

    return logEntry;
}

function connectWebSocket(service) {
    disconnectWebSocket();

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws/logs`;

    ws = new WebSocket(wsUrl);

    ws.onopen = () => {
        console.warn('WebSocket connected');
        updateConnectionStatus(true);

        ws.send(JSON.stringify({ service: service }));

        const viewer = document.getElementById('logViewer');
        viewer.classList.remove('loading', 'empty');
    };

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);

        if (data.type === 'anchor') {
            const isRestart = currentAnchor !== null;
            resetScrollbackState({ file: data.file, offset: data.offset });
            if (isRestart) {
                // Insert a separator so the user knows pitrac restarted
                const sep = document.createElement('div');
                sep.className = 'log-entry info';
                const sepContent = document.createElement('span');
                sepContent.className = 'log-content';
                sepContent.textContent = '— pitrac restarted —';
                sep.appendChild(sepContent);
                document.getElementById('logViewer').appendChild(sep);
                logBuffer.push(sep);
            }
            return;
        }

        if (!isPaused) {
            appendLog(data);
        }
    };

    ws.onerror = (error) => {
        console.error('WebSocket error:', error);
        updateConnectionStatus(false);
    };

    ws.onclose = () => {
        console.warn('WebSocket disconnected');
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
    const content = logData.message || logData.content || '';
    const logEntry = makeLogEntry(content);

    // Track error/warning stats on live append
    if (content.includes('ERROR') || content.includes('[error]')) {
        stats.errors++;
    } else if (content.includes('WARN') || content.includes('[warning]')) {
        stats.warnings++;
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

        logEntry.insertBefore(timestamp, logEntry.firstChild);
    }

    viewer.appendChild(logEntry);

    logBuffer.push(logEntry);
    // Trim oldest (front) when live tail grows past the cap
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

async function loadOlderHistory() {
    if (historyLoading || reachedStart || !oldestCursor || !currentService) return;

    historyLoading = true;
    const viewer = document.getElementById('logViewer');
    const prevScrollHeight = viewer.scrollHeight;

    try {
        const url = `/api/logs/history?service=${encodeURIComponent(currentService)}&file=${encodeURIComponent(oldestCursor.file)}&before=${oldestCursor.offset}&lines=200`;
        const response = await fetch(url);
        const data = await response.json();

        if (data.reset) {
            // Server says cursor is stale (file truncated) — clear and let live stream re-anchor
            clearLogs();
            resetScrollbackState(null);
            historyLoading = false;
            return;
        }

        const lines = data.lines || [];
        if (lines.length > 0) {
            const fragment = document.createDocumentFragment();
            const newEntries = lines.map(line => makeLogEntry(line));
            newEntries.forEach(entry => fragment.appendChild(entry));

            viewer.insertBefore(fragment, viewer.firstChild);

            // Prepend to buffer (oldest first); trim from newest end if over ceiling
            logBuffer.unshift(...newEntries);
            while (logBuffer.length > maxTotalLines) {
                const trimmed = logBuffer.pop();
                trimmed.remove();
            }
        }

        oldestCursor = data.next || null;
        if (data.next === null) reachedStart = true;

        // Restore scroll so the previously visible content stays in place
        viewer.scrollTop = viewer.scrollHeight - prevScrollHeight;
    } catch (err) {
        console.error('Failed to load log history:', err);
    }

    historyLoading = false;
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

    const iconEl = document.getElementById('pauseIcon');
    if (isPaused) {
        button.classList.add('paused');
        btnText.textContent = 'Resume';
        iconEl.setAttribute('data-lucide', 'play');
    } else {
        button.classList.remove('paused');
        btnText.textContent = 'Pause';
        iconEl.setAttribute('data-lucide', 'pause');

        const viewer = document.getElementById('logViewer');
        viewer.scrollTop = viewer.scrollHeight;
    }
    if (typeof lucide !== 'undefined') lucide.createIcons();
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

    const viewer = document.getElementById('logViewer');
    viewer.addEventListener('scroll', () => {
        if (viewer.scrollTop < 50) {
            loadOlderHistory();
        }
    });
});

window.addEventListener('beforeunload', () => {
    disconnectWebSocket();
});
