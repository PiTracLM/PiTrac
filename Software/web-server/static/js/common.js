// Common functionality for all PiTrac pages
/* exported setTheme, controlPiTrac, requireStrobeSafe, escapeHtml, toast, confirmDialog, api, openSocket, onPiTracStatus, formatNumber */

if (typeof lucide !== 'undefined') {
    lucide.createIcons();
}

// -- Helpers --

const HTML_ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

function escapeHtml(value) {
    return String(value ?? '').replace(/[&<>"']/g, c => HTML_ESCAPES[c]);
}

function formatNumber(value, decimals) {
    const n = Number(value);
    return value == null || Number.isNaN(n) ? '--' : n.toFixed(decimals);
}

async function api(path, { method = 'GET', body } = {}) {
    const res = await fetch(path, {
        method,
        headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
        body: body === undefined ? undefined : JSON.stringify(body),
    });
    const text = await res.text();
    let data = null;
    try { data = text ? JSON.parse(text) : null; } catch { data = null; }
    if (!res.ok) {
        const detail = data && typeof data.detail === 'string' && data.detail;
        throw new Error((data && (data.error || data.message)) || detail || `${res.status} ${res.statusText}`);
    }
    return data;
}

function openSocket(path, onMessage, { onOpen, onClose } = {}) {
    let ws = null;
    let timer = null;
    let stopped = false;
    const url = `${location.protocol === 'https:' ? 'wss:' : 'ws:'}//${location.host}${path}`;
    const connect = () => {
        if (stopped || (ws && ws.readyState <= WebSocket.OPEN)) return;
        clearTimeout(timer);
        timer = null;
        ws = new WebSocket(url);
        ws.binaryType = 'arraybuffer';
        ws.onopen = () => onOpen && onOpen();
        ws.onmessage = (e) => onMessage(typeof e.data === 'string' ? JSON.parse(e.data) : e.data);
        ws.onclose = () => {
            if (onClose) onClose();
            if (!stopped && !timer) timer = setTimeout(connect, 3000);
        };
        ws.onerror = () => ws.close();
    };
    const onVisible = () => { if (!document.hidden) connect(); };
    document.addEventListener('visibilitychange', onVisible);
    connect();
    return {
        send(msg) { if (ws && ws.readyState === WebSocket.OPEN) ws.send(JSON.stringify(msg)); },
        close() {
            stopped = true;
            clearTimeout(timer);
            document.removeEventListener('visibilitychange', onVisible);
            if (ws) ws.close();
        },
    };
}

function toast(message, type = 'info', { sticky = type === 'error', actionHref = null, actionLabel = null, onAction = null } = {}) {
    const region = document.getElementById('toast-region');
    if (!region) return;
    const el = document.createElement('div');
    el.className = `alert alert-${type} max-w-sm whitespace-normal`;
    const text = document.createElement('span');
    text.textContent = message;
    el.append(text);
    if (actionLabel && (actionHref || onAction)) {
        const action = document.createElement(actionHref ? 'a' : 'button');
        action.className = 'btn btn-sm whitespace-nowrap';
        action.textContent = actionLabel;
        if (actionHref) action.href = actionHref;
        if (onAction) action.addEventListener('click', () => { el.remove(); onAction(); });
        el.append(action);
    }
    if (sticky) {
        const close = document.createElement('button');
        close.className = 'btn btn-sm btn-ghost btn-square';
        close.setAttribute('aria-label', 'Dismiss');
        close.textContent = '✕';
        close.addEventListener('click', () => el.remove());
        el.append(close);
    } else {
        setTimeout(() => el.remove(), 4000);
    }
    region.append(el);
}

function confirmDialog({ title, body, confirmLabel = 'Confirm', danger = false }) {
    const dialog = document.getElementById('confirm-dialog');
    document.getElementById('confirm-dialog-title').textContent = title;
    document.getElementById('confirm-dialog-body').textContent = body;
    const ok = document.getElementById('confirm-dialog-ok');
    ok.textContent = confirmLabel;
    ok.className = `btn ${danger ? 'btn-error' : 'btn-primary'}`;
    dialog.returnValue = '';
    dialog.showModal();
    return new Promise((resolve) => {
        dialog.addEventListener('close', () => resolve(dialog.returnValue === 'confirm'), { once: true });
    });
}

// -- Theme --

let currentTheme = 'system';

function getSystemTheme() {
    return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

function applyTheme(theme) {
    document.documentElement.setAttribute('data-theme', theme === 'system' ? getSystemTheme() : theme);
    document.querySelectorAll('.theme-btn').forEach(b => b.classList.toggle('active', b.dataset.mode === theme));
}

function setTheme(theme) {
    currentTheme = theme;
    localStorage.setItem('pitrac-theme', theme);
    applyTheme(theme);
}

function initTheme() {
    currentTheme = localStorage.getItem('pitrac-theme') || 'system';
    applyTheme(currentTheme);
}

window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => {
    if (currentTheme === 'system') applyTheme('system');
});

// -- PiTrac Controls --

// Every other status from /api/pitrac/{start,stop,restart} is a failure or a refusal
const PITRAC_OK_STATUSES = ['started', 'already_running', 'stopped', 'not_running'];

let pitracActionInFlight = false;

async function controlPiTrac(action) {
    if ((action === 'start' || action === 'restart') && !(await requireStrobeSafe())) {return;}

    pitracActionInFlight = true;
    document.querySelectorAll('.control-btn').forEach(b => { b.disabled = true; });
    const spinner = document.createElement('span');
    spinner.className = 'loading loading-spinner loading-xs';
    const btn = document.getElementById(`pitrac-${action}-btn`);
    if (btn) btn.prepend(spinner);

    try {
        const data = await api(`/api/pitrac/${action}`, { method: 'POST' });
        if (!PITRAC_OK_STATUSES.includes(data.status)) throw new Error(data.message || `Failed to ${action} PiTrac`);
        toast(data.message, 'success');
    } catch (err) {
        toast(err.message, 'error', { actionHref: '/logs', actionLabel: 'Open logs' });
    } finally {
        spinner.remove();
        setTimeout(() => {
            pitracActionInFlight = false;
            window.checkPiTracStatus();
        }, 1000);
    }
}

function updatePiTracButtons(isRunning) {
    const set = (id, visible) => {
        const el = document.getElementById(id);
        if (el) el.classList.toggle('hidden', !visible);
    };
    set('pitrac-start-btn', !isRunning);
    set('pitrac-stop-btn', isRunning);
    set('pitrac-restart-item', isRunning);
    document.querySelectorAll('.control-btn').forEach(b => { b.disabled = false; });
}

// -- Status Polling --

const piTracStatusSubscribers = [];

function onPiTracStatus(fn) {
    piTracStatusSubscribers.push(fn);
}

function updateStatusPill({ is_running, pid, offline }) {
    const pill = document.getElementById('pitrac-status-pill');
    if (!pill) return;
    const state = offline ? 'offline' : is_running ? 'running' : 'stopped';
    const label = { running: 'Running', stopped: 'Stopped', offline: 'Offline' }[state];
    pill.className = `status-pill is-${state}`;
    if (pill.textContent !== label) pill.textContent = label;
    pill.title = offline ? 'Cannot reach the PiTrac web server'
        : is_running ? `PiTrac is running (PID ${pid})` : 'PiTrac is stopped';
}

async function checkPiTracStatus() {
    let status;
    try {
        const data = await api('/api/pitrac/status');
        status = { is_running: !!data.is_running, pid: data.pid ?? null, offline: false };
    } catch {
        status = { is_running: false, pid: null, offline: true };
    }
    updateStatusPill(status);
    if (!pitracActionInFlight && !status.offline) updatePiTracButtons(status.is_running);
    piTracStatusSubscribers.forEach(fn => {
        try {
            fn(status);
        } catch (err) {
            console.error('PiTrac status subscriber failed:', err);
        }
    });
    return status.is_running;
}

// -- Strobe Safety --

async function requireStrobeSafe() {
    try {
        const data = await api('/api/strobe-safety');
        if (data.safe) return true;
        showStrobeSafetyModal(data.reason || '');
        return false;
    } catch {
        showStrobeSafetyModal('Could not verify strobe safety. Check the connection to the PiTrac web server.');
        return false;
    }
}

function showStrobeSafetyModal(reason) {
    const modal = document.getElementById('strobe-safety-modal');
    if (!modal) return;
    const msg = document.getElementById('strobe-safety-msg');
    if (msg) msg.textContent = reason || 'V3 board requires strobe calibration before use.';
    modal.showModal();
    if (typeof lucide !== 'undefined') lucide.createIcons();
}

// -- Init --

document.addEventListener('click', (e) => {
    const menu = document.getElementById('nav-dropdown');
    if (menu && menu.open && (!menu.contains(e.target) || e.target.closest('ul a, ul button'))) menu.open = false;
});

document.addEventListener('DOMContentLoaded', () => {
    initTheme();
    checkPiTracStatus();
    // Indirect call so page-specific wrappers (e.g. dashboard.js) take effect.
    setInterval(() => window.checkPiTracStatus(), 5000);
});
