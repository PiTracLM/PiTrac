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
            btn.disabled = true;
            btn.title = 'PiTrac is already running';
        });
        stopBtns.forEach(btn => {
            btn.disabled = false;
            btn.title = 'Stop PiTrac';
        });
        restartBtns.forEach(btn => {
            btn.disabled = false;
            btn.title = 'Restart PiTrac';
        });
    } else {
        startBtns.forEach(btn => {
            btn.disabled = false;
            btn.title = 'Start PiTrac';
        });
        stopBtns.forEach(btn => {
            btn.disabled = true;
            btn.title = 'PiTrac is not running';
        });
        restartBtns.forEach(btn => {
            btn.disabled = true;
            btn.title = 'PiTrac is not running';
        });
    }
}

document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    initDropdown();
});