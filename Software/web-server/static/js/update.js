let currentBranch = null;
let selectedBranch = null;
let isUpdating = false;

async function loadBranches() {
    try {
        const resp = await fetch('/api/update/branches');
        const data = await resp.json();
        if (data.status !== 'ok') {
            showBanner(data.message, 'error');
            return;
        }

        currentBranch = data.current_branch;
        const select = document.getElementById('branchSelect');
        select.innerHTML = '';

        data.branches.forEach(b => {
            const opt = document.createElement('option');
            opt.value = b.name;
            opt.textContent = b.name + (b.name === currentBranch ? ' (current)' : '');
            if (b.name === currentBranch) opt.selected = true;
            select.appendChild(opt);
        });

        onBranchChange();
    } catch (e) {
        showBanner('Failed to load branches: ' + e.message, 'error');
    }
}

function onBranchChange() {
    const select = document.getElementById('branchSelect');
    selectedBranch = select.value;
    const isSwitching = selectedBranch && selectedBranch !== currentBranch;

    document.getElementById('updateBtn').style.display = isSwitching ? 'none' : '';
    document.getElementById('switchBtn').style.display = isSwitching ? '' : 'none';

    if (!isSwitching) {
        document.getElementById('commitsSection').style.display = 'none';
    }
}

async function checkForUpdates() {
    const btn = document.getElementById('checkBtn');
    btn.disabled = true;
    btn.classList.add('loading');

    try {
        const resp = await fetch('/api/update/check');
        const data = await resp.json();

        if (data.status !== 'ok') {
            showBanner(data.message, 'error');
            return;
        }

        document.getElementById('currentBranch').textContent = data.current_branch;
        document.getElementById('currentHash').textContent = data.current_hash;
        document.getElementById('lastBuild').textContent = formatTime(data.last_build);
        document.getElementById('lastCheck').textContent = formatTime(data.last_check);

        if (data.warning) {
            document.getElementById('warningRow').style.display = '';
            document.getElementById('warningText').textContent = data.warning;
        } else {
            document.getElementById('warningRow').style.display = 'none';
        }

        if (data.updates_available && data.commits.length > 0) {
            showCommits(data.commits);
            document.getElementById('updateBtn').disabled = false;
        } else {
            document.getElementById('commitsSection').style.display = 'none';
            document.getElementById('updateBtn').disabled = true;
            showBanner('Already up to date on ' + data.current_branch, 'success');
        }
    } catch (e) {
        showBanner('Check failed: ' + e.message, 'error');
    } finally {
        btn.disabled = false;
        btn.classList.remove('loading');
    }
}

function showCommits(commits) {
    document.getElementById('commitCount').textContent = commits.length;
    const list = document.getElementById('commitList');
    list.innerHTML = '';

    commits.forEach(c => {
        const item = document.createElement('div');
        item.className = 'commit-item';
        item.innerHTML =
            '<span class="commit-hash">' + escapeHtml(c.hash) + '</span>' +
            '<span class="commit-message">' + escapeHtml(c.message) + '</span>' +
            '<span class="commit-meta">' + escapeHtml(c.author) + ' · ' + escapeHtml(c.time) + '</span>';
        list.appendChild(item);
    });

    document.getElementById('commitsSection').style.display = '';
}

async function startUpdate(force) {
    const body = { force: force };
    const branch = document.getElementById('branchSelect').value;
    if (branch && branch !== currentBranch) {
        body.branch = branch;
    }

    setUpdatingState(true);

    try {
        const resp = await fetch('/api/update/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
        const data = await resp.json();

        if (data.status !== 'started') {
            showBanner(data.message, 'error');
            setUpdatingState(false);
            return;
        }

        document.getElementById('logSection').style.display = '';
        showBanner('Update in progress. The server will restart when done.', 'info');
        pollStatus();
    } catch (e) {
        showBanner('Failed to start update: ' + e.message, 'error');
        setUpdatingState(false);
    }
}

function switchBranch() {
    startUpdate(true);
}

async function cancelUpdate() {
    try {
        await fetch('/api/update/cancel', { method: 'POST' });
        setUpdatingState(false);
        showBanner('Update cancelled.', 'info');
    } catch (e) {
        showBanner('Failed to cancel: ' + e.message, 'error');
    }
}

let pollTimer = null;

function pollStatus() {
    if (pollTimer) clearInterval(pollTimer);

    pollTimer = setInterval(async () => {
        try {
            const resp = await fetch('/api/update/status');
            const data = await resp.json();

            renderLog(data.log_tail || []);

            if (data.status === 'idle' || data.status === 'failed') {
                clearInterval(pollTimer);
                pollTimer = null;
                setUpdatingState(false);

                if (data.status === 'failed') {
                    showBanner('Update failed: ' + (data.error || 'unknown error'), 'error');
                }
            }
        } catch (e) {
            // Server likely restarting — wait and try to reconnect
            clearInterval(pollTimer);
            pollTimer = null;
            showBanner('Server restarting... reconnecting.', 'info');
            waitForRestart();
        }
    }, 1500);
}

function waitForRestart() {
    let attempts = 0;
    const maxAttempts = 40; // ~60s

    const timer = setInterval(async () => {
        attempts++;
        try {
            const resp = await fetch('/health');
            if (resp.ok) {
                clearInterval(timer);
                showBanner('Update complete! Server restarted.', 'success');
                setUpdatingState(false);
                loadBranches();
                loadStatus();
            }
        } catch (e) {
            if (attempts >= maxAttempts) {
                clearInterval(timer);
                showBanner('Server did not come back after 60s. Check logs.', 'error');
                setUpdatingState(false);
            }
        }
    }, 1500);
}

function renderLog(lines) {
    const el = document.getElementById('buildLog');
    el.innerHTML = '';

    lines.forEach(line => {
        const div = document.createElement('div');
        div.className = 'log-line';
        if (line.includes('[ERROR]')) div.classList.add('error');
        else if (line.includes('[UPDATE]')) div.classList.add('update');
        else if (line.includes('[GIT]')) div.classList.add('git');
        else div.classList.add('build');
        div.textContent = line;
        el.appendChild(div);
    });

    el.scrollTop = el.scrollHeight;
}

function clearLog() {
    document.getElementById('buildLog').innerHTML = '';
}

function setUpdatingState(updating) {
    isUpdating = updating;
    document.getElementById('checkBtn').disabled = updating;
    document.getElementById('updateBtn').disabled = updating;
    document.getElementById('switchBtn').disabled = updating;
    document.getElementById('branchSelect').disabled = updating;
    document.getElementById('cancelBtn').style.display = updating ? '' : 'none';

    if (!updating) {
        document.getElementById('updateBtn').style.display = '';
        document.getElementById('switchBtn').style.display = 'none';
        onBranchChange();
    }
}

function showBanner(message, type) {
    let banner = document.querySelector('.update-banner');
    if (!banner) {
        banner = document.createElement('div');
        banner.className = 'update-banner';
        const section = document.querySelector('.update-section');
        section.insertBefore(banner, section.querySelector('.status-card'));
    }
    banner.className = 'update-banner ' + type;
    banner.textContent = message;
}

async function loadStatus() {
    try {
        const resp = await fetch('/api/update/status');
        const data = await resp.json();

        if (!data.configured) {
            showBanner('Update system not configured. Run sudo ./build.sh dev first.', 'error');
            return;
        }

        document.getElementById('lastBuild').textContent = formatTime(data.last_build);
        document.getElementById('lastCheck').textContent = formatTime(data.last_check);

        if (data.last_update) {
            showBanner('Last update: ' + formatTime(data.last_update), 'success');
        }

        if (data.status === 'updating') {
            setUpdatingState(true);
            document.getElementById('logSection').style.display = '';
            renderLog(data.log_tail || []);
            pollStatus();
        }
    } catch (e) {
        console.error('Failed to load status:', e);
    }
}

function formatTime(iso) {
    if (!iso) return '--';
    const d = new Date(iso);
    if (isNaN(d.getTime())) return iso;
    return d.toLocaleString();
}

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}

document.addEventListener('DOMContentLoaded', () => {
    loadStatus();
    loadBranches();
});
