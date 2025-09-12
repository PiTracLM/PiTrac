// Dashboard-specific functionality (theme and dropdown handled by common.js)
let ws = null;

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

    ws.onopen = () => {
        // WebSocket connected
        document.getElementById('ws-status-dot').classList.remove('disconnected');
    };

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        updateDisplay(data);
    };

    ws.onclose = () => {
        // WebSocket disconnected
        document.getElementById('ws-status-dot').classList.add('disconnected');
        setTimeout(connectWebSocket, 3000);
    };

    ws.onerror = (error) => {
        console.error('WebSocket error:', error);
    };
}

function updateDisplay(data) {
    const updateMetric = (id, value) => {
        const element = document.getElementById(id);
        const oldValue = element.textContent;
        if (oldValue !== value.toString()) {
            element.textContent = value;
            element.parentElement.classList.add('updated');
            setTimeout(() => {
                element.parentElement.classList.remove('updated');
            }, 500);
        }
    };

    updateMetric('speed', data.speed || '0.0');
    updateMetric('carry', data.carry || '0.0');
    updateMetric('launch_angle', data.launch_angle || '0.0');
    updateMetric('side_angle', data.side_angle || '0.0');
    updateMetric('back_spin', data.back_spin || '0');
    updateMetric('side_spin', data.side_spin || '0');

    // Update ball ready status indicator
    updateBallStatus(data.result_type, data.message, data.pitrac_running);

    if (data.timestamp) {
        const date = new Date(data.timestamp);
        document.getElementById('timestamp').textContent = date.toLocaleTimeString();
    }

    // Update images - only show images for actual hits, clear for status messages
    const imageGrid = document.getElementById('image-grid');
    const resultType = (data.result_type || '').toLowerCase();

    // Only show images for hit results
    if (resultType.includes('hit') && data.images && data.images.length > 0) {
        imageGrid.innerHTML = data.images.map((img, idx) =>
            `<img src="/images/${img}" alt="Shot ${idx + 1}" class="shot-image" loading="lazy" onclick="openImage('${img}')">`
        ).join('');
    } else if (!resultType.includes('hit')) {
        imageGrid.innerHTML = '';
    }
}

function updateBallStatus(resultType, message, isPiTracRunning) {
    const indicator = document.getElementById('ball-ready-indicator');
    const statusTitle = document.getElementById('ball-status-title');
    const statusMessage = document.getElementById('ball-status-message');

    indicator.classList.remove('initializing', 'waiting', 'stabilizing', 'ready', 'hit', 'error');

    if (isPiTracRunning === false) {
        indicator.classList.add('error');
        statusTitle.textContent = 'System Stopped';
        statusMessage.textContent = 'PiTrac is not running - click Start to begin';
        return;
    }

    if (resultType) {
        const normalizedType = resultType.toLowerCase();

        if (normalizedType.includes('initializing')) {
            indicator.classList.add('initializing');
            statusTitle.textContent = 'System Initializing';
            statusMessage.textContent = message || 'Starting up PiTrac system...';
        } else if (normalizedType.includes('waiting for ball')) {
            indicator.classList.add('waiting');
            statusTitle.textContent = 'Waiting for Ball';
            statusMessage.textContent = message || 'Please place ball on tee';
        } else if (normalizedType.includes('waiting for simulator')) {
            indicator.classList.add('waiting');
            statusTitle.textContent = 'Waiting for Simulator';
            statusMessage.textContent = message || 'Waiting for simulator to be ready';
        } else if (normalizedType.includes('pausing') || normalizedType.includes('stabilization')) {
            indicator.classList.add('stabilizing');
            statusTitle.textContent = 'Ball Detected';
            statusMessage.textContent = message || 'Waiting for ball to stabilize...';
        } else if (normalizedType.includes('ball ready') || normalizedType.includes('ready')) {
            indicator.classList.add('ready');
            statusTitle.textContent = 'Ready to Hit!';
            statusMessage.textContent = message || 'Ball is ready - take your shot!';
        } else if (normalizedType.includes('hit')) {
            indicator.classList.add('hit');
            statusTitle.textContent = 'Ball Hit!';
            statusMessage.textContent = message || 'Processing shot data...';
        } else if (normalizedType.includes('error')) {
            indicator.classList.add('error');
            statusTitle.textContent = 'Error';
            statusMessage.textContent = message || 'An error occurred';
        } else if (normalizedType.includes('multiple balls')) {
            indicator.classList.add('error');
            statusTitle.textContent = 'Multiple Balls Detected';
            statusMessage.textContent = message || 'Please remove extra balls';
        } else {
            statusTitle.textContent = 'System Status';
            statusMessage.textContent = message || resultType;
        }
    }
}

function openImage(imgPath) {
    window.open(`/images/${imgPath}`, '_blank');
}

async function resetShot() {
    try {
        const response = await fetch('/api/reset', { method: 'POST' });
        if (response.ok) {
        }
    } catch (error) {
        console.error('Error resetting shot:', error);
    }
}


let originalCheckPiTracStatus;
const dashboardCheckPiTracStatus = async function() {
    if (!originalCheckPiTracStatus) {
        originalCheckPiTracStatus = window.checkPiTracStatus;
    }
    const isRunning = await originalCheckPiTracStatus();
    
    if (!isRunning) {
        updateBallStatus(null, null, false);
    }
    
    return isRunning;
}

function showStatusMessage(message, type = 'info') {
    const statusMessage = document.getElementById('ball-status-message');
    if (statusMessage) {
        const originalMessage = statusMessage.textContent;
        statusMessage.textContent = message;
        statusMessage.className = `ball-status-message ${type}`;

        setTimeout(() => {
            statusMessage.textContent = originalMessage;
            statusMessage.className = 'ball-status-message';
        }, 3000);
    }
}

document.addEventListener('DOMContentLoaded', () => {
    connectWebSocket();
    
    updateBallStatus('Initializing', 'System starting up...');
    
    if (window.checkPiTracStatus) {
        originalCheckPiTracStatus = window.checkPiTracStatus;
        window.checkPiTracStatus = dashboardCheckPiTracStatus;
    }
    
    document.addEventListener('visibilitychange', () => {
        if (!document.hidden && (!ws || ws.readyState !== WebSocket.OPEN)) {
            connectWebSocket();
        }
    });
});