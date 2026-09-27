/* global api, openSocket */
let sock = null;
let socketGen = 0;
let isPaused = false;
let pausedQueue = [];
let currentService = null;
let logBuffer = [];
const maxLogLines = 2000;
// Hard ceiling for scrollback growth; trims from the newest end so live-tail trimming stays unaffected
const maxTotalLines = 5000;
const stickToBottomPx = 40;
let stats = { lines: 0, errors: 0, warnings: 0 };

let currentAnchor = null;
let oldestCursor = null;
let reachedStart = false;
let historyLoading = false;

const viewerEl = () => document.getElementById('logViewer');

async function loadServices() {
    const select = document.getElementById('serviceSelect');
    try {
        const data = await api('/api/logs/services');
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
        select.innerHTML = '<option value="">Error loading services</option>';
    }
}

function changeService() {
    const select = document.getElementById('serviceSelect');
    const statusEl = document.getElementById('serviceStatus');

    closeSocket();
    clearLogs();
    resetScrollbackState(null);
    currentService = select.value || null;

    if (!currentService) {
        statusEl.classList.add('hidden');
        viewerEl().dataset.empty = 'Not connected';
        updateConnectionStatus(false);
        return;
    }

    const status = select.options[select.selectedIndex].dataset.status;
    statusEl.textContent = status.charAt(0).toUpperCase() + status.slice(1);
    statusEl.className = 'badge badge-soft ' + (status === 'running' ? 'badge-success' : 'badge-error');

    viewerEl().dataset.empty = 'Waiting for log lines...';
    connectSocket(currentService);
}

function resetScrollbackState(anchor) {
    currentAnchor = anchor;
    oldestCursor = anchor ? { file: anchor.file, offset: anchor.offset } : null;
    reachedStart = false;
    historyLoading = false;
}

function levelOf(content) {
    if (content.includes('ERROR') || content.includes('[error]')) return 'error';
    if (content.includes('WARN') || content.includes('[warning]')) return 'warning';
    if (content.includes('INFO') || content.includes('[info]')) return 'info';
    if (content.includes('DEBUG') || content.includes('[debug]')) return 'debug';
    return null;
}

function makeLogEntry(content, level = levelOf(content)) {
    const logEntry = document.createElement('div');
    logEntry.className = 'log-entry' + (level ? ' ' + level : '');
    logEntry.dataset.level = level || '';
    logEntry.dataset.text = content.toLowerCase();

    const logContent = document.createElement('span');
    logContent.className = 'log-content';
    logContent.textContent = content;
    logEntry.appendChild(logContent);

    applyFilter(logEntry);
    return logEntry;
}

function applyFilter(entry) {
    const errorsOnly = document.getElementById('errorsOnly').checked;
    const needle = document.getElementById('logFilter').value.trim().toLowerCase();
    entry.hidden = (errorsOnly && entry.dataset.level !== 'error')
        || (needle !== '' && !entry.dataset.text.includes(needle));
}

function refilter() {
    logBuffer.forEach(applyFilter);
}

function connectSocket(service) {
    const gen = ++socketGen;
    const live = fn => (...args) => { if (gen === socketGen) fn(...args); };

    sock = openSocket('/ws/logs', live(handleMessage), {
        onOpen: live(() => {
            // The server replays its tail on every connection, so start from an empty view
            clearLogs();
            resetScrollbackState(null);
            pausedQueue = [];
            viewerEl().dataset.empty = 'Waiting for log lines...';
            updateConnectionStatus(true);
            sock.send({ service });
        }),
        onClose: live(() => updateConnectionStatus(false)),
    });
}

function closeSocket() {
    socketGen++;
    if (sock) sock.close();
    sock = null;
    pausedQueue = [];
}

function handleMessage(data) {
    if (data.type === 'anchor') {
        const isRestart = currentAnchor !== null;
        resetScrollbackState({ file: data.file, offset: data.offset });
        if (isRestart) addEntry(makeLogEntry('pitrac restarted', 'info'));
        return;
    }

    if (isPaused) {
        pausedQueue.push(data);
        if (pausedQueue.length > maxLogLines) pausedQueue.shift();
        updatePauseButton();
        return;
    }
    appendLog(data);
}

function updateConnectionStatus(connected) {
    const dot = document.getElementById('connectionDot');
    dot.classList.toggle('bg-success', connected);
    dot.classList.toggle('bg-base-content/30', !connected);
    document.getElementById('connectionText').textContent = connected ? 'Connected' : 'Reconnecting...';
    if (!connected && !currentService) {
        document.getElementById('connectionText').textContent = 'Not connected';
    }
}

function isNearBottom(viewer) {
    return viewer.scrollHeight - viewer.scrollTop - viewer.clientHeight <= stickToBottomPx;
}

function addEntry(entry) {
    const viewer = viewerEl();
    const follow = isNearBottom(viewer);
    viewer.appendChild(entry);
    logBuffer.push(entry);
    if (logBuffer.length > maxLogLines) {
        logBuffer.shift().remove();
    }
    if (follow) viewer.scrollTop = viewer.scrollHeight;
}

function timestampText(raw) {
    let date;
    if (typeof raw === 'string' && raw.length > 10) {
        date = new Date(parseInt(raw) / 1000);
    } else {
        date = new Date(raw);
    }
    return isNaN(date.getTime()) ? '' : date.toLocaleTimeString();
}

function appendLog(logData) {
    const isServerError = typeof logData.error === 'string';
    const content = isServerError ? logData.error : (logData.message || logData.content || '');
    const level = isServerError ? 'error' : levelOf(content);
    const logEntry = makeLogEntry(content, level);

    if (level === 'error') {
        stats.errors++;
    } else if (level === 'warning') {
        stats.warnings++;
    }

    if (logData.timestamp) {
        const timestamp = document.createElement('span');
        timestamp.className = 'log-timestamp';
        timestamp.textContent = timestampText(logData.timestamp);
        logEntry.insertBefore(timestamp, logEntry.firstChild);
    }

    addEntry(logEntry);
    stats.lines++;
    updateStats();
}

async function loadOlderHistory() {
    if (historyLoading || reachedStart || !oldestCursor || !currentService) return;

    historyLoading = true;
    const viewer = viewerEl();
    const prevScrollHeight = viewer.scrollHeight;
    const service = currentService;
    const cursor = oldestCursor;

    try {
        const url = `/api/logs/history?service=${encodeURIComponent(service)}&file=${encodeURIComponent(cursor.file)}&before=${cursor.offset}&lines=200`;
        const data = await api(url);

        // The service changed or the stream re-anchored while the request was in flight
        if (service !== currentService || cursor !== oldestCursor) {
            historyLoading = false;
            return;
        }

        if (data.reset) {
            // Server says cursor is stale (file truncated), clear and let live stream re-anchor
            clearLogs();
            resetScrollbackState(null);
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
                logBuffer.pop().remove();
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

function updatePauseButton() {
    const button = document.getElementById('pauseButton');
    const queued = pausedQueue.length;
    const label = isPaused ? (queued ? `Resume (${queued})` : 'Resume') : 'Pause';
    const hint = isPaused ? 'Resume log stream' : 'Pause log stream';
    button.querySelector('.btn-text').textContent = label;
    button.setAttribute('aria-label', hint);
    button.title = hint;
    button.classList.toggle('btn-warning', isPaused);
    button.classList.toggle('btn-ghost', !isPaused);
}

function togglePause() {
    isPaused = !isPaused;
    const icon = document.getElementById('pauseIcon');
    icon.innerHTML = `<i data-lucide="${isPaused ? 'play' : 'pause'}" class="icon-sm"></i>`;
    if (typeof lucide !== 'undefined') lucide.createIcons();

    if (!isPaused) {
        const queued = pausedQueue;
        pausedQueue = [];
        queued.forEach(appendLog);
        const viewer = viewerEl();
        viewer.scrollTop = viewer.scrollHeight;
    }
    updatePauseButton();
}

function clearLogs() {
    viewerEl().innerHTML = '';
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
    a.download = `${currentService || 'logs'}_${new Date().toISOString().replace(/[:.]/g, '-')}.log`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
}

document.addEventListener('DOMContentLoaded', () => {
    loadServices();

    document.getElementById('serviceSelect').addEventListener('change', changeService);
    document.getElementById('pauseButton').addEventListener('click', togglePause);
    document.getElementById('clearButton').addEventListener('click', clearLogs);
    document.getElementById('downloadButton').addEventListener('click', downloadLogs);
    document.getElementById('errorsOnly').addEventListener('change', refilter);
    document.getElementById('logFilter').addEventListener('input', refilter);

    const viewer = viewerEl();
    viewer.addEventListener('scroll', () => {
        if (viewer.scrollTop < 50) {
            loadOlderHistory();
        }
    });
});

window.addEventListener('beforeunload', closeSocket);
