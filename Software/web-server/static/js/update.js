/* global api, escapeHtml, confirmDialog */
/* exported checkForUpdates, switchBranch, cancelUpdate, startUpdate, onBranchChange */
let currentBranch = null;
let selectedBranch = null;

const $ = id => document.getElementById(id);

async function loadBranches() {
    try {
        const data = await api('/api/update/branches');
        if (data.status !== 'ok') {
            showBanner(data.message, 'error');
            return;
        }

        currentBranch = data.current_branch;
        $('currentBranch').textContent = currentBranch;
        const select = $('branchSelect');
        select.innerHTML = '';

        const addOption = (name, label) => {
            const opt = document.createElement('option');
            opt.value = name;
            opt.textContent = label;
            select.appendChild(opt);
        };

        const current = data.branches.find(b => b.name === currentBranch);
        addOption(currentBranch, currentBranch + ' (current)' + (current ? ', ' + current.last_commit : ''));
        data.branches
            .filter(b => b.name !== currentBranch)
            .forEach(b => addOption(b.name, b.name + ', ' + b.last_commit));
        select.value = currentBranch;

        onBranchChange();
    } catch (e) {
        showBanner('Failed to load branches: ' + e.message, 'error');
    }
}

function onBranchChange() {
    selectedBranch = $('branchSelect').value;
    const isSwitching = selectedBranch && selectedBranch !== currentBranch;

    $('updateBtn').classList.toggle('hidden', isSwitching);
    $('switchBtn').classList.toggle('hidden', !isSwitching);
    $('commitsSection').classList.add('hidden');
}

async function checkForUpdates() {
    const btn = $('checkBtn');
    setBtnLoading(btn, true);

    try {
        const query = selectedBranch ? '?branch=' + encodeURIComponent(selectedBranch) : '';
        const data = await api('/api/update/check' + query);

        if (data.status !== 'ok') {
            showBanner(data.message, 'error');
            return;
        }

        $('currentBranch').textContent = data.current_branch;
        $('currentHash').textContent = data.current_hash;
        $('lastBuild').textContent = formatTime(data.last_build);
        $('lastCheck').textContent = formatTime(data.last_check);

        $('warningRow').classList.toggle('hidden', !data.warning);
        if (data.warning) $('warningText').textContent = data.warning;

        if (data.updates_available && data.commits.length > 0) {
            showCommits(data.commits);
            $('updateBtn').disabled = false;
            hideBanner();
        } else {
            $('commitsSection').classList.add('hidden');
            $('updateBtn').disabled = true;
            showBanner('Already up to date on ' + (selectedBranch || data.current_branch), 'success');
        }
    } catch (e) {
        showBanner('Check failed: ' + e.message, 'error');
    } finally {
        setBtnLoading(btn, false);
    }
}

function showCommits(commits) {
    $('commitCount').textContent = commits.length;
    const list = $('commitList');
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

    $('commitsSection').classList.remove('hidden');
}

async function startUpdate(force) {
    const body = { force: force };
    const branch = $('branchSelect').value;
    const isSwitching = branch && branch !== currentBranch;
    if (isSwitching) {
        body.branch = branch;
    }

    const confirmed = await confirmDialog(isSwitching
        ? {
            title: `Switch to ${branch} and rebuild?`,
            body: 'This rebuilds everything and can take several minutes.',
            confirmLabel: 'Switch and rebuild',
        }
        : {
            title: 'Update and rebuild?',
            body: 'PiTrac stops until the rebuild finishes.',
            confirmLabel: 'Update and rebuild',
        });
    if (!confirmed) return;

    setUpdatingState(true);

    try {
        const data = await api('/api/update/start', { method: 'POST', body });

        if (data.status !== 'started') {
            showBanner(data.message, 'error');
            setUpdatingState(false);
            return;
        }

        $('logSection').classList.remove('hidden');
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
    const confirmed = await confirmDialog({
        title: 'Cancel the update?',
        body: 'Cancelling now may leave PiTrac half-installed.',
        confirmLabel: 'Cancel update',
        danger: true,
    });
    if (!confirmed) return;

    try {
        const data = await api('/api/update/cancel', { method: 'POST' });
        if (data.status !== 'cancelled') {
            showBanner(data.message, 'error');
            return;
        }
        stopPolling();
        setUpdatingState(false);
        showBanner('Update cancelled.', 'error');
    } catch (e) {
        showBanner('Failed to cancel: ' + e.message, 'error');
    }
}

let pollTimer = null;

function stopPolling() {
    if (pollTimer) clearInterval(pollTimer);
    pollTimer = null;
}

function pollStatus() {
    stopPolling();

    pollTimer = setInterval(async () => {
        try {
            const data = await api('/api/update/status');

            renderLog(data.log_tail || []);

            if (data.status === 'idle' || data.status === 'failed') {
                stopPolling();
                setUpdatingState(false);
                showLastResult(data);
            }
        } catch {
            // One failed poll is retried; only an unreachable /health means a restart
            try {
                const health = await fetch('/health');
                if (health.ok) return;
            } catch {
                // fall through to restart handling
            }
            stopPolling();
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
            const health = await fetch('/health');
            if (!health.ok) throw new Error('not ready');
            const data = await api('/api/update/status');
            clearInterval(timer);
            if (data.status === 'updating') {
                setUpdatingState(true);
                pollStatus();
                return;
            }
            setUpdatingState(false);
            if (data.last_result === 'success') {
                showBanner('Update complete. Server restarted.', 'success');
            } else {
                showLastResult(data, 'Server restarted, but the update did not report success.');
            }
            loadBranches();
            loadStatus();
        } catch {
            if (attempts >= maxAttempts) {
                clearInterval(timer);
                showBanner('Server did not come back after 60s. Check logs.', 'error');
                setUpdatingState(false);
            }
        }
    }, 1500);
}

function renderLog(lines) {
    const el = $('buildLog');
    const follow = el.scrollHeight - el.scrollTop - el.clientHeight <= 40;
    el.innerHTML = '';

    lines.forEach(line => {
        const div = document.createElement('div');
        div.className = 'log-line';
        if (line.includes('[ERROR]')) div.classList.add('error');
        else if (line.includes('[UPDATE]')) div.classList.add('update');
        else if (line.includes('[GIT]')) div.classList.add('git');
        div.textContent = line;
        el.appendChild(div);
    });

    if (follow) el.scrollTop = el.scrollHeight;
}

function setUpdatingState(updating) {
    $('checkBtn').disabled = updating;
    $('updateBtn').disabled = updating;
    $('switchBtn').disabled = updating;
    $('branchSelect').disabled = updating;
    $('cancelBtn').classList.toggle('hidden', !updating);

    if (!updating) onBranchChange();
}

function showBanner(message, type) {
    const banner = $('updateBanner');
    banner.className = 'alert alert-' + type + ' -mt-2';
    banner.textContent = message;
}

function hideBanner() {
    $('updateBanner').className = 'alert hidden';
}

function showLastResult(data, fallback) {
    if (data.last_result === 'success') {
        showBanner('Last update: ' + formatTime(data.last_update), 'success');
    } else if (data.last_result === 'failed') {
        showBanner('Last update failed: ' + (data.error || 'unknown error'), 'error');
    } else if (data.last_result === 'cancelled') {
        showBanner('Last update was cancelled.', 'error');
    } else if (fallback) {
        showBanner(fallback, 'error');
    }
}

function setBtnLoading(btn, loading) {
    if (loading) {
        btn.disabled = true;
        btn._savedInner = btn.innerHTML;
        btn.innerHTML = '<span class="loading loading-spinner loading-xs"></span>';
    } else {
        btn.disabled = false;
        if (btn._savedInner) {
            btn.innerHTML = btn._savedInner;
            btn._savedInner = null;
        }
    }
}

async function loadStatus() {
    try {
        const data = await api('/api/update/status');

        $('notConfigured').classList.toggle('hidden', data.configured);
        $('updateControls').classList.toggle('hidden', !data.configured);
        if (!data.configured) return false;

        $('lastBuild').textContent = formatTime(data.last_build);
        $('lastCheck').textContent = formatTime(data.last_check);

        showLastResult(data);

        if (data.status === 'updating') {
            setUpdatingState(true);
            $('logSection').classList.remove('hidden');
            renderLog(data.log_tail || []);
            pollStatus();
            return false;
        }
        return true;
    } catch (e) {
        console.error('Failed to load status:', e);
        showBanner('Failed to load update status: ' + e.message, 'error');
        return false;
    }
}

function formatTime(iso) {
    if (!iso) return '--';
    const d = new Date(iso);
    if (isNaN(d.getTime())) return iso;
    return d.toLocaleString();
}

document.addEventListener('DOMContentLoaded', async () => {
    if (await loadStatus()) loadBranches();
});
