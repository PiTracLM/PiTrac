// Shot history page
let selectedSessionId = null;

function emptyState(msg, isError = false) {
    const tone = isError ? 'text-error' : 'opacity-40';
    return `<div class="text-sm ${tone} text-center py-4">${escapeHtml(msg)}</div>`;
}

function loadingState() {
    return '<div class="flex justify-center py-6"><span class="loading loading-spinner loading-md text-primary"></span></div>';
}

async function loadStorageUsage() {
    try {
        const resp = await fetch('/api/storage/usage');
        const data = await resp.json();
        const el = document.getElementById('storage-usage');
        const used = data.used_mb >= 1024
            ? (data.used_mb / 1024).toFixed(1) + ' GB'
            : data.used_mb + ' MB';
        const cap = data.cap_mb >= 1024
            ? (data.cap_mb / 1024).toFixed(1) + ' GB'
            : data.cap_mb + ' MB';
        el.textContent = `${used} of ${cap} used`;
    } catch (err) {
        console.error('Failed to load storage usage:', err);
        document.getElementById('storage-usage').textContent = 'Storage info unavailable';
    }
}

async function loadSessions() {
    const container = document.getElementById('sessions-list');
    try {
        const resp = await fetch('/api/sessions');
        const sessions = await resp.json();

        if (sessions.length === 0) {
            container.innerHTML = emptyState('No sessions yet.');
            return;
        }

        const list = document.createElement('ul');
        list.className = 'list bg-base-100 rounded-box';

        sessions.forEach(session => {
            const row = document.createElement('li');
            row.className = 'list-row items-center cursor-pointer hover:bg-base-200 transition-colors';
            row.dataset.sessionId = session.id;

            const started = new Date(session.started_at).toLocaleString();
            const label = session.label ? `<div class="font-medium text-sm">${escapeHtml(session.label)}</div>` : '';

            row.innerHTML = `
                <div class="list-col-grow min-w-0">
                    ${label}
                    <div class="text-xs opacity-60">${started}</div>
                    <div class="text-xs opacity-50 mt-0.5">${session.shot_count} shot${session.shot_count !== 1 ? 's' : ''}</div>
                </div>
                <button class="btn btn-xs btn-ghost text-error delete-session-btn" data-session-id="${session.id}" title="Delete session" aria-label="Delete session">
                    <i data-lucide="trash-2" class="icon-sm"></i>
                </button>
            `;

            list.appendChild(row);
        });

        container.innerHTML = '';
        container.appendChild(list);

        if (typeof lucide !== 'undefined') lucide.createIcons();

        // re-select the previously selected session if it still exists
        if (selectedSessionId !== null) {
            if (container.querySelector(`[data-session-id="${selectedSessionId}"]`)) {
                highlightSession(selectedSessionId);
            } else {
                selectedSessionId = null;
                clearShots();
            }
        }
    } catch (err) {
        console.error('Failed to load sessions:', err);
        container.innerHTML = emptyState('Failed to load sessions.', true);
    }
}

function clearShots() {
    document.getElementById('shots-heading').textContent = 'Shots';
    document.getElementById('shots-list').innerHTML = emptyState('Select a session to view shots.');
    clearDetail();
}

function clearDetail() {
    document.getElementById('shot-detail').innerHTML = emptyState('Select a shot to view details.');
}

async function loadShots(sessionId) {
    const container = document.getElementById('shots-list');
    container.innerHTML = loadingState();

    try {
        const resp = await fetch(`/api/sessions/${sessionId}/shots`);
        if (!resp.ok) {
            container.innerHTML = emptyState('Session not found.', true);
            return;
        }

        const shots = await resp.json();
        document.getElementById('shots-heading').textContent = `Shots (${shots.length})`;

        if (shots.length === 0) {
            container.innerHTML = emptyState('No shots in this session.');
            return;
        }

        const table = document.createElement('table');
        table.className = 'table table-xs w-full';
        table.innerHTML = `
            <thead>
                <tr>
                    <th>Time</th>
                    <th>Type</th>
                    <th>Speed</th>
                    <th>Carry</th>
                    <th>Launch°</th>
                    <th>Side°</th>
                </tr>
            </thead>
            <tbody id="shots-tbody"></tbody>
        `;

        container.innerHTML = '';
        container.appendChild(table);

        const tbody = document.getElementById('shots-tbody');
        shots.forEach(shot => {
            const tr = document.createElement('tr');
            tr.className = 'cursor-pointer hover:bg-base-300';
            tr.dataset.shotId = shot.id;

            const t = new Date(shot.created_at).toLocaleTimeString();
            const speed = shot.speed != null ? shot.speed.toFixed(1) : '--';
            const carry = shot.carry != null ? shot.carry.toFixed(0) : '--';
            const launch = shot.launch_angle != null ? shot.launch_angle.toFixed(1) : '--';
            const side = shot.side_angle != null ? shot.side_angle.toFixed(1) : '--';

            tr.innerHTML = `
                <td class="text-xs">${t}</td>
                <td class="text-xs"><span class="badge badge-sm badge-ghost">${escapeHtml(shot.result_type || '--')}</span></td>
                <td class="text-xs">${speed}</td>
                <td class="text-xs">${carry}</td>
                <td class="text-xs">${launch}</td>
                <td class="text-xs">${side}</td>
            `;

            tbody.appendChild(tr);
        });

    } catch (err) {
        console.error('Failed to load shots:', err);
        container.innerHTML = emptyState('Failed to load shots.', true);
    }
}

async function loadShotDetail(shotId) {
    const container = document.getElementById('shot-detail');
    container.innerHTML = loadingState();

    try {
        const resp = await fetch(`/api/shots/${shotId}`);
        if (!resp.ok) {
            container.innerHTML = emptyState('Shot not found.', true);
            return;
        }

        const shot = await resp.json();
        const ts = new Date(shot.created_at).toLocaleString();

        const metrics = [
            ['Time', ts],
            ['Type', shot.result_type || '--'],
            ['Speed', shot.speed != null ? shot.speed.toFixed(1) + ' mph' : '--'],
            ['Carry', shot.carry != null ? shot.carry.toFixed(0) + ' yd' : '--'],
            ['Launch Angle', shot.launch_angle != null ? shot.launch_angle.toFixed(1) + '°' : '--'],
            ['Side Angle', shot.side_angle != null ? shot.side_angle.toFixed(1) + '°' : '--'],
            ['Back Spin', shot.back_spin != null ? shot.back_spin.toFixed(0) + ' rpm' : '--'],
            ['Side Spin', shot.side_spin != null ? shot.side_spin.toFixed(0) + ' rpm' : '--'],
        ];

        if (shot.message) {
            metrics.push(['Message', shot.message]);
        }

        let html = '<div class="flex flex-col gap-1">';
        metrics.forEach(([label, value]) => {
            html += `
                <div class="flex justify-between items-center py-1.5 border-b border-base-300 last:border-0">
                    <span class="text-xs opacity-60">${escapeHtml(label)}</span>
                    <span class="text-xs font-medium">${escapeHtml(String(value))}</span>
                </div>
            `;
        });
        html += '</div>';

        if (shot.images && shot.images.length > 0) {
            html += '<div class="mt-3"><div class="text-xs opacity-60 mb-2 uppercase tracking-wide">Images</div>';
            html += '<div id="shot-images" class="flex flex-col gap-2">';
            shot.images.forEach((img, idx) => {
                html += `<div class="shot-img-wrapper" data-idx="${idx}" data-path="${escapeHtml(img.file_path)}">
                    <img class="rounded border border-base-300 w-full" src="/images/${escapeHtml(img.file_path)}" alt="${escapeHtml(img.kind || 'image')}">
                    <div class="text-xs opacity-40 mt-0.5">${escapeHtml(img.kind || img.file_path)}</div>
                </div>`;
            });
            html += '</div></div>';
        }

        container.innerHTML = html;

        // wire onerror for images via JS — no inline handlers
        container.querySelectorAll('img').forEach(img => {
            img.addEventListener('error', () => {
                const wrapper = img.closest('.shot-img-wrapper');
                if (wrapper) {
                    wrapper.innerHTML = '<div class="text-xs opacity-40 italic py-2">Image expired or not available</div>';
                }
            });
        });

    } catch (err) {
        console.error('Failed to load shot detail:', err);
        container.innerHTML = emptyState('Failed to load shot.', true);
    }
}

async function deleteSession(sessionId) {
    if (!confirm('Delete this session and all its shots? This cannot be undone.')) return;

    try {
        const resp = await fetch(`/api/sessions/${sessionId}`, { method: 'DELETE' });
        if (!resp.ok) {
            alert('Failed to delete session.');
            return;
        }
        if (selectedSessionId === sessionId) {
            selectedSessionId = null;
            clearShots();
        }
        await loadSessions();
        await loadStorageUsage();
    } catch (err) {
        console.error('Failed to delete session:', err);
        alert('Failed to delete session.');
    }
}

function escapeHtml(str) {
    return String(str)
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

function highlightSession(sessionId) {
    document.querySelectorAll('#sessions-list [data-session-id]').forEach(el => {
        el.classList.toggle('bg-primary/10', parseInt(el.dataset.sessionId, 10) === sessionId);
    });
}

function highlightShot(shotId) {
    document.querySelectorAll('#shots-tbody tr').forEach(tr => {
        tr.classList.toggle('bg-primary/10', parseInt(tr.dataset.shotId, 10) === shotId);
    });
}

document.addEventListener('DOMContentLoaded', () => {
    loadStorageUsage();
    loadSessions();

    // session clicks via delegation
    document.getElementById('sessions-list').addEventListener('click', async e => {
        const deleteBtn = e.target.closest('.delete-session-btn');
        if (deleteBtn) {
            e.stopPropagation();
            const sid = parseInt(deleteBtn.dataset.sessionId, 10);
            await deleteSession(sid);
            return;
        }

        const card = e.target.closest('[data-session-id]');
        if (!card) return;

        const sid = parseInt(card.dataset.sessionId, 10);
        selectedSessionId = sid;
        clearDetail();
        highlightSession(sid);
        await loadShots(sid);
    });

    // shot row clicks via delegation
    document.getElementById('shots-list').addEventListener('click', async e => {
        const tr = e.target.closest('tr[data-shot-id]');
        if (!tr) return;

        const sid = parseInt(tr.dataset.shotId, 10);
        highlightShot(sid);
        await loadShotDetail(sid);
    });
});
