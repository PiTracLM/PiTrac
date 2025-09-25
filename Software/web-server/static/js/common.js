// Common functionality for all PiTrac pages

// Theme management
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
        const systemBtn = document.querySelector('.theme-btn[data-theme="system"]');
        if (systemBtn) systemBtn.classList.add('active');
    } else {
        root.setAttribute('data-theme', theme);
        const themeBtn = document.querySelector(`.theme-btn[data-theme="${theme}"]`);
        if (themeBtn) themeBtn.classList.add('active');
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
}

// Listen for system theme changes
window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', (e) => {
    if (currentTheme === 'system') {
        applyTheme('system');
    }
});

function initDropdown() {
    const dropdown = document.querySelector('.dropdown');
    const toggle = document.querySelector('.dropdown-toggle');
    
    if (toggle && dropdown) {
        toggle.addEventListener('click', (e) => {
            e.stopPropagation();
            dropdown.classList.toggle('active');
        });
        
        document.addEventListener('click', () => {
            dropdown.classList.remove('active');
        });
        
        const dropdownMenu = document.querySelector('.dropdown-menu');
        if (dropdownMenu) {
            dropdownMenu.addEventListener('click', (e) => {
                e.stopPropagation();
            });
        }
    }
}

async function controlPiTrac(action) {
    const buttonMap = {
        'start': ['pitrac-start-btn-desktop', 'pitrac-start-btn-mobile'],
        'stop': ['pitrac-stop-btn-desktop', 'pitrac-stop-btn-mobile'],
        'restart': ['pitrac-restart-btn-desktop', 'pitrac-restart-btn-mobile']
    };
    
    const buttons = buttonMap[action].map(id => document.getElementById(id)).filter(btn => btn);
    
    document.querySelectorAll('.control-btn').forEach(btn => {
        btn.disabled = true;
    });
    
    buttons.forEach(btn => {
        if (btn) {
            btn.classList.add('loading');
        }
    });
    
    try {
        const response = await fetch(`/api/pitrac/${action}`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            }
        });
        
        if (!response.ok) {
            const errorData = await response.json();
            throw new Error(errorData.error || `Failed to ${action} PiTrac`);
        }
        
        const data = await response.json();
        
        if (typeof showStatusMessage === 'function') {
            showStatusMessage(data.message, 'success');
        }
        
        setTimeout(() => {
            if (typeof checkSystemStatus === 'function') {
                checkSystemStatus();
            }
        }, 2000);
        
    } catch (error) {
        console.error(`Error ${action}ing PiTrac:`, error);
        if (typeof showStatusMessage === 'function') {
            showStatusMessage(error.message || `Failed to ${action} PiTrac`, 'error');
        }
    } finally {
        buttons.forEach(btn => {
            if (btn) {
                btn.classList.remove('loading');
            }
        });
        
        setTimeout(() => {
            if (typeof checkPiTracStatus === 'function') {
                checkPiTracStatus();
            } else {
                document.querySelectorAll('.control-btn').forEach(btn => {
                    btn.disabled = false;
                });
            }
        }, 1000);
    }
}

function updatePiTracButtons(isRunning) {
    const startBtns = ['pitrac-start-btn-desktop', 'pitrac-start-btn-mobile']
        .map(id => document.getElementById(id))
        .filter(btn => btn);
    
    const stopBtns = ['pitrac-stop-btn-desktop', 'pitrac-stop-btn-mobile']
        .map(id => document.getElementById(id))
        .filter(btn => btn);
    
    const restartBtns = ['pitrac-restart-btn-desktop', 'pitrac-restart-btn-mobile']
        .map(id => document.getElementById(id))
        .filter(btn => btn);
    
    if (isRunning) {
        startBtns.forEach(btn => {
            btn.style.display = 'none';
        });
        stopBtns.forEach(btn => {
            btn.style.display = '';
            btn.disabled = false;
            btn.title = 'Stop PiTrac';
        });
        restartBtns.forEach(btn => {
            btn.style.display = '';
            btn.disabled = false;
            btn.title = 'Restart PiTrac';
        });
    } else {
        startBtns.forEach(btn => {
            btn.style.display = '';
            btn.disabled = false;
            btn.title = 'Start PiTrac';
        });
        stopBtns.forEach(btn => {
            btn.style.display = 'none';
        });
        restartBtns.forEach(btn => {
            btn.style.display = 'none';
        });
    }
}

async function checkSystemStatus() {
    try {
        const response = await fetch('/health');
        if (response.ok) {
            const data = await response.json();

            const mqDot = document.getElementById('mq-status-dot');
            if (mqDot) {
                if (data.zeromq_connected) {
                    mqDot.classList.remove('disconnected');
                } else {
                    mqDot.classList.add('disconnected');
                }
            }

            return data;
        }
    } catch (error) {
        console.error('Error checking system status:', error);
    }
    return null;
}

async function checkPiTracStatus() {
    try {
        const response = await fetch('/api/pitrac/status');
        const status = await response.json();
        updatePiTracButtons(status.is_running);
        
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
        
        const camera2Container = document.getElementById('camera2-status-container');
        const camera2Dot = document.getElementById('pitrac-camera2-status-dot');
        
        if (status.mode === 'single') {
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
            if (camera2Container) {
                camera2Container.style.display = 'none';
            }
        }
        
        return status.is_running;
    } catch (error) {
        console.error('Failed to check PiTrac status:', error);
        return false;
    }
}

document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    initDropdown();

    checkSystemStatus();
    checkPiTracStatus();
    
    setInterval(checkSystemStatus, 5000);
    setInterval(checkPiTracStatus, 5000);
});