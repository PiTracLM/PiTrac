let ws = null;
let currentTheme = 'system';

function getSystemTheme() {
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

function applyTheme(theme) {
    const root = document.documentElement;

    root.removeAttribute('data-theme');

    document.querySelectorAll('.theme-btn').forEach(btn => {
        btn.classList.remove('active');
    });

    if (theme === 'system') {
        const systemTheme = getSystemTheme();
        root.setAttribute('data-theme', systemTheme);
        document.querySelector('.theme-btn[data-theme="system"]').classList.add('active');
    } else {
        root.setAttribute('data-theme', theme);
        document.querySelector(`.theme-btn[data-theme="${theme}"]`).classList.add('active');
    }
}

function setTheme(theme) {
    currentTheme = theme;
    localStorage.setItem('pitrac-theme', theme);
    applyTheme(theme);
}

function initTheme() {
    const savedTheme = localStorage.getItem('pitrac-theme') || 'system';
    currentTheme = savedTheme;
    applyTheme(savedTheme);

    // Initialize dropdown menu
    initDropdown();
}

function initDropdown() {
    const dropdown = document.querySelector('.dropdown');
    const toggle = document.querySelector('.dropdown-toggle');

    if (toggle && dropdown) {
        toggle.addEventListener('click', (e) => {
            e.stopPropagation();
            dropdown.classList.toggle('active');
        });

        // Close dropdown when clicking outside
        document.addEventListener('click', () => {
            dropdown.classList.remove('active');
        });

        // Prevent dropdown from closing when clicking inside
        const dropdownMenu = document.querySelector('.dropdown-menu');
        if (dropdownMenu) {
            dropdownMenu.addEventListener('click', (e) => {
                e.stopPropagation();
            });
        }
    }
}

window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
    if (currentTheme === 'system') {
        applyTheme('system');
    }
});

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

    // These elements are commented out in HTML:
    // document.getElementById('result_type').textContent = data.result_type || 'Waiting...';
    // document.getElementById('message').textContent = data.message || '';

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
            // Shot reset successfully
        }
    } catch (error) {
        console.error('Error resetting shot:', error);
    }
}

async function checkSystemStatus() {
    try {
        const response = await fetch('/health');
        if (response.ok) {
            const data = await response.json();

            const mqDot = document.getElementById('mq-status-dot');
            if (data.activemq_connected) {
                mqDot.classList.remove('disconnected');
            } else {
                mqDot.classList.add('disconnected');
            }

            const pitracDot = document.getElementById('pitrac-status-dot');
            if (data.pitrac_running) {
                pitracDot.classList.remove('disconnected');
            } else {
                pitracDot.classList.add('disconnected');
            }

            const camera2Dot = document.getElementById('pitrac-camera2-status-dot');
            if (camera2Dot && !data.pitrac_running) {
                camera2Dot.classList.add('disconnected');
            }
        }
    } catch (error) {
        console.error('Error checking system status:', error);
    }
}

document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    connectWebSocket();
    checkSystemStatus();

    setInterval(checkSystemStatus, 5000);

    checkPiTracStatus();
    setInterval(checkPiTracStatus, 5000);

    updateBallStatus('Initializing', 'System starting up...');

    document.addEventListener('visibilitychange', () => {
        if (!document.hidden && (!ws || ws.readyState !== WebSocket.OPEN)) {
            connectWebSocket();
        }
    });
});

async function controlPiTrac(action) {
    const desktopButton = document.getElementById(`pitrac-${action}-btn-desktop`);
    const mobileButton = document.getElementById(`pitrac-${action}-btn-mobile`);

    if (desktopButton) {
        desktopButton.disabled = true;
        desktopButton.classList.add('loading');
    }
    if (mobileButton) {
        mobileButton.disabled = true;
        mobileButton.classList.add('loading');
    }

    try {
        const response = await fetch(`/api/pitrac/${action}`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            }
        });

        const result = await response.json();

        showStatusMessage(result.message || `PiTrac ${action} completed`, result.status === 'error' ? 'error' : 'success');

        setTimeout(() => {
            checkPiTracStatus();
        }, 1000);

    } catch (error) {
        console.error(`Failed to ${action} PiTrac:`, error);
        showStatusMessage(`Failed to ${action} PiTrac: ${error.message}`, 'error');
    } finally {
        if (desktopButton) {
            desktopButton.disabled = false;
            desktopButton.classList.remove('loading');
        }
        if (mobileButton) {
            mobileButton.disabled = false;
            mobileButton.classList.remove('loading');
        }
    }
}

async function checkPiTracStatus() {
    try {
        const response = await fetch('/api/pitrac/status');
        const status = await response.json();

        updatePiTracButtons(status.is_running);

        if (!status.is_running) {
            updateBallStatus(null, null, false);
        }

        // Camera 1 status
        const statusDot = document.getElementById('pitrac-status-dot');
        if (statusDot) {
            if (status.camera1_pid) {
                statusDot.classList.add('connected');
                statusDot.classList.remove('disconnected');
                statusDot.title = `PiTrac Camera 1 Running (PID: ${status.camera1_pid})`;
            } else {
                statusDot.classList.remove('connected');
                statusDot.classList.add('disconnected');
                statusDot.title = 'PiTrac Camera 1 Stopped';
            }
        }

        // Camera 2 status (only shown in single-Pi mode)
        const camera2Container = document.getElementById('camera2-status-container');
        const camera2Dot = document.getElementById('pitrac-camera2-status-dot');

        if (status.mode === 'single') {
            // Show camera2 indicator in single-Pi mode
            if (camera2Container) {
                camera2Container.style.display = 'flex';
            }

            if (camera2Dot) {
                if (status.camera2_pid) {
                    camera2Dot.classList.add('connected');
                    camera2Dot.classList.remove('disconnected');
                    camera2Dot.title = `PiTrac Camera 2 Running (PID: ${status.camera2_pid})`;
                } else {
                    camera2Dot.classList.remove('connected');
                    camera2Dot.classList.add('disconnected');
                    camera2Dot.title = 'PiTrac Camera 2 Stopped';
                }
            }
        } else {
            // Hide camera2 indicator in dual-Pi mode
            if (camera2Container) {
                camera2Container.style.display = 'none';
            }
        }
    } catch (error) {
        console.error('Failed to check PiTrac status:', error);
    }
}

function updatePiTracButtons(isRunning) {
    const startBtnDesktop = document.getElementById('pitrac-start-btn-desktop');
    const stopBtnDesktop = document.getElementById('pitrac-stop-btn-desktop');
    const restartBtnDesktop = document.getElementById('pitrac-restart-btn-desktop');

    const startBtnMobile = document.getElementById('pitrac-start-btn-mobile');
    const stopBtnMobile = document.getElementById('pitrac-stop-btn-mobile');
    const restartBtnMobile = document.getElementById('pitrac-restart-btn-mobile');

    if (startBtnDesktop) {
        startBtnDesktop.disabled = isRunning;
        startBtnDesktop.style.display = isRunning ? 'none' : 'inline-flex';
    }

    if (stopBtnDesktop) {
        stopBtnDesktop.disabled = !isRunning;
        stopBtnDesktop.style.display = isRunning ? 'inline-flex' : 'none';
    }

    if (restartBtnDesktop) {
        restartBtnDesktop.disabled = !isRunning;
        restartBtnDesktop.style.display = isRunning ? 'inline-flex' : 'none';
    }

    if (startBtnMobile) {
        startBtnMobile.disabled = isRunning;
        startBtnMobile.style.display = isRunning ? 'none' : 'flex';
    }

    if (stopBtnMobile) {
        stopBtnMobile.disabled = !isRunning;
        stopBtnMobile.style.display = isRunning ? 'flex' : 'none';
    }

    if (restartBtnMobile) {
        restartBtnMobile.disabled = !isRunning;
        restartBtnMobile.style.display = isRunning ? 'flex' : 'none';
    }
}

function showStatusMessage(message, type = 'info') {
    // Status message: [type] message

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
