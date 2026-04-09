// Dashboard-specific functionality (theme and dropdown handled by common.js)
let ws = null;
let piTracRunning = false;

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

    ws.onopen = () => {
        document.getElementById('ws-status-dot').classList.remove('disconnected');
    };

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (!piTracRunning) {
            piTracRunning = true;
            const metricsPanel = document.getElementById('metrics-panel');
            if (metricsPanel) metricsPanel.style.opacity = '1';
        }
        if (data.type === 'image_ready') {
            handleImageReady(data.filename);
        } else {
            updateDisplay(data);
        }
    };

    ws.onclose = () => {
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
        if (!element) return;
        const unitSpan = element.querySelector('.metric-unit');
        const currentText = element.firstChild && element.firstChild.nodeType === Node.TEXT_NODE
            ? element.firstChild.textContent.trim()
            : '';
        const newValue = String(value);

        if (currentText !== newValue) {
            if (element.firstChild && element.firstChild.nodeType === Node.TEXT_NODE) {
                element.firstChild.textContent = newValue;
            } else {
                element.insertBefore(document.createTextNode(newValue), unitSpan);
            }
            element.classList.add('updated');
            setTimeout(() => {
                element.classList.remove('updated');
            }, 500);
        }
    };

    updateMetric('speed', data.speed || '0.0');
    updateMetric('launch_angle', data.launch_angle || '0.0');
    updateMetric('side_angle', data.side_angle || '0.0');
    updateMetric('back_spin', data.back_spin || '0');
    updateMetric('side_spin', data.side_spin || '0');

    // Update ball status strip
    updateBallStatus(data.result_type, data.message, data.pitrac_running);

    if (data.timestamp) {
        const date = new Date(data.timestamp);
        document.getElementById('timestamp').textContent = date.toLocaleTimeString();
    }

    const resultType = (data.result_type || '').toLowerCase();
    if (resultType.includes('stabilization') || resultType.includes('pausing')) {
        const imageInner = document.getElementById('image-panel-inner');
        imageInner.className = 'image-panel-inner';
        imageInner.innerHTML =
            '<div class="image-empty-state">' +
                '<div class="empty-icon"></div>' +
                '<div class="empty-text">Waiting for shot...</div>' +
            '</div>';
    }
}

function handleImageReady(filename) {
    const imageInner = document.getElementById('image-panel-inner');
    const ts = Date.now();
    imageInner.className = 'image-panel-inner';
    imageInner.innerHTML =
        `<img src="/images/${filename}?t=${ts}" alt="Shot image" class="shot-image" onclick="openImage('${filename}')">`;
}

function updateBallStatus(resultType, message, isPiTracRunning) {
    const strip = document.getElementById('status-strip');
    const title = document.getElementById('status-strip-title');
    const msg = document.getElementById('status-strip-message');
    const resetBtn = document.getElementById('btn-reset');

    strip.classList.remove('initializing', 'waiting', 'stabilizing', 'ready', 'hit', 'error');

    resetBtn.style.display = 'none';

    if (isPiTracRunning === false) {
        strip.classList.add('error');
        title.textContent = 'System Stopped';
        msg.textContent = 'PiTrac is not running \u2014 click Start to begin';
        return;
    }

    if (resultType) {
        const normalizedType = resultType.toLowerCase();

        if (normalizedType.includes('initializing')) {
            strip.classList.add('initializing');
            title.textContent = 'System Initializing';
            msg.textContent = message || 'Starting up PiTrac system...';
        } else if (normalizedType.includes('waiting for ball')) {
            strip.classList.add('waiting');
            title.textContent = 'Waiting for Ball';
            msg.textContent = message || 'Please place ball on tee';
        } else if (normalizedType.includes('waiting for simulator')) {
            strip.classList.add('waiting');
            title.textContent = 'Waiting for Simulator';
            msg.textContent = message || 'Waiting for simulator to be ready';
        } else if (normalizedType.includes('pausing') || normalizedType.includes('stabilization')) {
            strip.classList.add('stabilizing');
            title.textContent = 'Ball Detected';
            msg.textContent = message || 'Waiting for ball to stabilize...';
        } else if (normalizedType.includes('ball ready') || normalizedType.includes('ready')) {
            strip.classList.add('ready');
            title.textContent = 'Ready to Hit!';
            msg.textContent = message || 'Ball is ready, take your shot';
        } else if (normalizedType.includes('hit')) {
            strip.classList.add('hit');
            title.textContent = 'Ball Hit!';
            msg.textContent = message || 'Processing shot data...';
            resetBtn.style.display = '';
        } else if (normalizedType.includes('error')) {
            strip.classList.add('error');
            title.textContent = 'Error';
            msg.textContent = message || 'An error occurred';
        } else if (normalizedType.includes('multiple balls')) {
            strip.classList.add('error');
            title.textContent = 'Multiple Balls Detected';
            msg.textContent = message || 'Please remove extra balls';
        } else {
            title.textContent = 'System Status';
            msg.textContent = message || resultType;
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
            // Clear the image panel on explicit reset
            const imageInner = document.getElementById('image-panel-inner');
            imageInner.className = 'image-panel-inner';
            imageInner.innerHTML =
                '<div class="image-empty-state">' +
                    '<div class="empty-icon"></div>' +
                    '<div class="empty-text">Waiting for shot...</div>' +
                '</div>';
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
    piTracRunning = isRunning;

    const metricsPanel = document.getElementById('metrics-panel');
    if (metricsPanel) {
        metricsPanel.style.opacity = isRunning ? '1' : '0.3';
    }

    if (!isRunning) {
        updateBallStatus(null, null, false);
    }

    return isRunning;
}

function showStatusMessage(message, type = 'info') {
    const statusMessage = document.getElementById('status-strip-message');
    if (statusMessage) {
        const originalMessage = statusMessage.textContent;
        statusMessage.textContent = message;
        statusMessage.className = `status-strip-message ${type}`;

        setTimeout(() => {
            statusMessage.textContent = originalMessage;
            statusMessage.className = 'status-strip-message';
        }, 3000);
    }
}

document.addEventListener('DOMContentLoaded', () => {
    connectWebSocket();

    updateBallStatus(null, null, false);

    // Check if a shot image already exists on disk (e.g. page refresh after a shot)
    const img = new Image();
    img.onload = () => handleImageReady('ball_exposure_candidates.png');
    img.src = '/images/ball_exposure_candidates.png?t=' + Date.now();

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
