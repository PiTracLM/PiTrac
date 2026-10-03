// Shot history page
/* global api, toast, confirmDialog, openSocket, formatNumber, formatSided */

const PAGE_SIZE = 50;

// Stems from the image filename defaults in configurations.json (gs_config.user_interface.kWebServer*)
const IMAGE_LABELS = {
    spin_ball_1_gray_image1: 'Spin, ball 1',
    spin_ball_2_gray_image1: 'Spin, ball 2',
    ball1_rotated_by_best_angles: 'Spin, best fit rotation',
    ball_exposure_candidates: 'Ball exposure candidates',
    log_cam2_last_strobed_img: 'Camera 2 strobed shot',
    log_ball_final_found_ball_img: 'Teed ball',
    log_cam1_search_area_img: 'Camera 1 search area',
};

const COLUMNS = [
    ['Speed (mph)', (s) => formatNumber(s.speed, 1)],
    ['Launch (°)', (s) => formatNumber(s.launch_angle, 1)],
    ['Side (°)', (s) => formatSided(s.side_angle, 1)],
    ['Back spin (rpm)', (s) => formatNumber(s.back_spin, 0)],
    ['Side spin (rpm)', (s) => formatSided(s.side_spin, 0)],
];

let sessions = [];
let hasMore = false;
let selectedId = null;
let shots = [];
let expandedId = null;
let shotsRequest = 0;
const shotDetails = new Map();

const byId = (id) => document.getElementById(id);

function el(tag, className = '', text = '') {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (text) node.textContent = text;
    return node;
}

function sessionLabel(iso) {
    const d = new Date(iso);
    const time = d.toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
    const today = new Date();
    const days = Math.round((new Date(today).setHours(0, 0, 0, 0) - new Date(d).setHours(0, 0, 0, 0)) / 86400000);
    if (days === 0) return `Today, ${time}`;
    if (days === 1) return `Yesterday, ${time}`;
    const opts = { month: 'short', day: 'numeric' };
    if (d.getFullYear() !== today.getFullYear()) opts.year = 'numeric';
    return `${d.toLocaleDateString([], opts)}, ${time}`;
}

const shotCount = (n) => `${n} shot${n === 1 ? '' : 's'}`;

function formatMb(mb) {
    return mb >= 1024 ? `${(mb / 1024).toFixed(1)} GB` : `${mb} MB`;
}

async function loadStorageUsage() {
    const target = byId('storage-usage');
    try {
        const data = await api('/api/storage/usage');
        target.textContent = `Images use ${formatMb(data.used_mb)} of ${formatMb(data.cap_mb)}`;
    } catch (err) {
        console.error('Failed to load storage usage:', err);
        target.textContent = 'Storage usage unavailable';
    }
}

// -- Sessions --

async function loadSessions({ more = false } = {}) {
    const offset = more ? sessions.length : 0;
    const limit = more ? PAGE_SIZE : Math.max(PAGE_SIZE, sessions.length);
    let data;
    try {
        data = await api(`/api/sessions?limit=${limit}&offset=${offset}`);
    } catch (err) {
        toast(`Could not load sessions: ${err.message}`, 'error');
        return;
    }
    sessions = more ? sessions.concat(data.sessions) : data.sessions;
    hasMore = data.has_more;
    renderSessions();
    if (!sessions.some(s => s.id === selectedId)) await selectSession(sessions.length ? sessions[0].id : null);
}

function renderSessions() {
    byId('history-empty').classList.toggle('hidden', sessions.length > 0);
    byId('history-main').classList.toggle('hidden', sessions.length === 0);

    const list = byId('sessions-list');
    list.replaceChildren(...sessions.map(s => {
        const row = el('div', 'session-row flex items-center gap-1 rounded-box hover:bg-base-300');
        row.dataset.sessionId = s.id;
        const pick = el('button', 'session-pick flex-1 min-w-0 text-left px-3 py-2');
        pick.append(el('div', 'text-sm font-medium truncate', sessionLabel(s.started_at)),
            el('div', 'text-xs opacity-60', shotCount(s.shot_count)));
        const del = el('button', 'session-delete btn btn-ghost btn-sm btn-square text-error');
        del.setAttribute('aria-label', `Delete session from ${sessionLabel(s.started_at)}`);
        del.title = 'Delete session';
        del.innerHTML = '<i data-lucide="trash-2" class="icon-sm"></i>';
        row.append(pick, del);
        return row;
    }));
    byId('sessions-more-wrap').classList.toggle('hidden', !hasMore);

    const select = byId('sessions-select');
    select.replaceChildren(...sessions.map(s => {
        const opt = el('option', '', `${sessionLabel(s.started_at)} (${shotCount(s.shot_count)})`);
        opt.value = s.id;
        return opt;
    }));
    if (hasMore) {
        const more = el('option', '', 'Load more sessions...');
        more.value = 'more';
        select.append(more);
    }
    highlightSession();
    if (typeof lucide !== 'undefined') lucide.createIcons({ nodes: [list] });
}

function highlightSession() {
    document.querySelectorAll('.session-row').forEach(row => {
        const on = Number(row.dataset.sessionId) === selectedId;
        row.classList.toggle('bg-primary/10', on);
        row.querySelector('.session-pick').setAttribute('aria-current', on ? 'true' : 'false');
    });
    if (selectedId !== null) byId('sessions-select').value = String(selectedId);
}

async function selectSession(id) {
    if (id !== selectedId) {
        expandedId = null;
        shots = [];
    }
    selectedId = id;
    highlightSession();
    if (id === null) return;
    renderSessionHeader();
    await loadShots();
}

async function deleteSession(id) {
    const session = sessions.find(s => s.id === id);
    if (!session) return;
    const ok = await confirmDialog({
        title: 'Delete this session?',
        body: `This removes the session from ${sessionLabel(session.started_at)}, its ${shotCount(session.shot_count)} and their images. It can't be undone.`,
        confirmLabel: 'Delete',
        danger: true,
    });
    if (!ok) return;
    try {
        await api(`/api/sessions/${id}`, { method: 'DELETE' });
    } catch (err) {
        toast(`Could not delete the session: ${err.message}`, 'error');
        return;
    }
    sessions = sessions.filter(s => s.id !== id);
    renderSessions();
    if (selectedId === id) await selectSession(sessions.length ? sessions[0].id : null);
    loadStorageUsage();
}

// -- Shots --

function renderSessionHeader() {
    const session = sessions.find(s => s.id === selectedId);
    if (!session) return;
    byId('session-title').textContent = sessionLabel(session.started_at);
    const speeds = shots.map(s => s.speed).filter(v => v != null);
    const parts = [shotCount(shots.length || session.shot_count)];
    if (speeds.length) parts.push(`Average ball speed ${formatNumber(speeds.reduce((a, b) => a + b, 0) / speeds.length, 1)} mph`);
    byId('session-summary').textContent = parts.join(' · ');
}

async function loadShots() {
    const request = ++shotsRequest;
    const id = selectedId;
    const wrap = byId('shots-wrap');
    wrap.classList.add('opacity-50');
    let rows;
    try {
        rows = await api(`/api/sessions/${id}/shots`);
    } catch (err) {
        if (request === shotsRequest) {
            wrap.classList.remove('opacity-50');
            toast(`Could not load shots: ${err.message}`, 'error');
        }
        return;
    }
    if (request !== shotsRequest) return;
    wrap.classList.remove('opacity-50');
    shots = rows;
    renderSessionHeader();
    renderShots();
}

function renderShots() {
    const wrap = byId('shots-wrap');
    if (!shots.length) {
        wrap.replaceChildren(el('p', 'p-6 text-center text-sm opacity-60', 'No shots in this session.'));
        return;
    }
    const table = el('table', 'table table-sm tabular-nums');
    const head = el('tr');
    head.append(el('th', '', 'Time'), ...COLUMNS.map(([label]) => el('th', 'text-right whitespace-nowrap', label)));
    const thead = el('thead');
    thead.append(head);
    table.append(thead);
    const body = el('tbody');
    shots.forEach(shot => {
        const tr = el('tr', 'shot-row cursor-pointer hover:bg-base-300');
        tr.tabIndex = 0;
        tr.dataset.shotId = shot.id;
        tr.setAttribute('aria-expanded', 'false');
        const time = new Date(shot.created_at).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit', second: '2-digit' });
        tr.append(el('td', 'whitespace-nowrap', time), ...COLUMNS.map(([, fmt]) => el('td', 'text-right whitespace-nowrap', fmt(shot))));
        body.append(tr);
    });
    table.append(body);
    wrap.replaceChildren(table);
    if (expandedId !== null && shots.some(s => s.id === expandedId)) expandShot(expandedId);
    else expandedId = null;
}

function collapseShot() {
    const open = byId('shots-wrap').querySelector('.shot-detail');
    if (open) open.remove();
    byId('shots-wrap').querySelectorAll('.shot-row[aria-expanded="true"]').forEach(tr => {
        tr.setAttribute('aria-expanded', 'false');
        tr.classList.remove('bg-base-300');
    });
}

function fitDetail() {
    const inner = byId('shots-wrap').querySelector('.shot-detail-inner');
    if (inner) inner.style.width = `${byId('shots-wrap').clientWidth}px`;
}

async function expandShot(id) {
    collapseShot();
    expandedId = id;
    const tr = byId('shots-wrap').querySelector(`.shot-row[data-shot-id="${id}"]`);
    if (!tr) return;
    tr.setAttribute('aria-expanded', 'true');
    tr.classList.add('bg-base-300');

    const detail = el('tr', 'shot-detail');
    const td = el('td', 'p-0 bg-base-100');
    td.colSpan = COLUMNS.length + 1;
    const inner = el('div', 'shot-detail-inner sticky left-0 p-4 flex flex-col gap-3');
    const spinner = el('div', 'flex justify-center py-4');
    spinner.append(el('span', 'loading loading-spinner loading-sm text-primary'));
    inner.append(spinner);
    td.append(inner);
    detail.append(td);
    tr.after(detail);
    fitDetail();

    let shot = shotDetails.get(id);
    if (!shot) {
        try {
            shot = await api(`/api/shots/${id}`);
            shotDetails.set(id, shot);
        } catch (err) {
            if (expandedId === id) inner.replaceChildren(el('p', 'text-sm text-error', `Could not load this shot: ${err.message}`));
            return;
        }
    }
    if (expandedId !== id || !inner.isConnected) return;
    renderDetail(inner, shot);
}

function renderDetail(inner, shot) {
    const captured = new Date(shot.created_at).toLocaleString([], { dateStyle: 'medium', timeStyle: 'medium' });
    inner.replaceChildren(el('p', 'text-sm opacity-60', `Captured ${captured}`));
    const images = shot.images || [];
    if (!images.length) {
        inner.append(el('p', 'text-sm opacity-60', 'Images expired or not available'));
        return;
    }
    const grid = el('div', 'grid grid-cols-2 sm:grid-cols-3 xl:grid-cols-4 gap-3');
    images.forEach(image => {
        const src = `/images/${encodeURI(image.file_path)}`;
        const label = IMAGE_LABELS[image.kind] || image.kind || image.file_path;
        const fig = el('figure', 'm-0 flex flex-col items-stretch gap-1 min-w-0');
        const link = el('a', 'block rounded-box border border-base-300 bg-base-300 overflow-hidden aspect-[4/3]');
        link.href = src;
        link.target = '_blank';
        link.rel = 'noopener';
        link.setAttribute('aria-label', `Open ${label} full size`);
        const img = el('img', 'w-full h-full object-contain');
        img.src = src;
        img.alt = label;
        img.loading = 'lazy';
        img.addEventListener('error', () => link.replaceChildren(
            el('span', 'flex h-full items-center justify-center p-2 text-xs opacity-60 text-center', 'Image expired or not available')));
        link.append(img);
        fig.append(link, el('figcaption', 'text-xs opacity-70 truncate', label));
        grid.append(fig);
    });
    inner.append(grid);
}

function toggleShot(tr) {
    const id = Number(tr.dataset.shotId);
    if (expandedId === id) {
        collapseShot();
        expandedId = null;
    } else {
        expandShot(id);
    }
}

// -- Init --

byId('sessions-list').addEventListener('click', (e) => {
    const row = e.target.closest('.session-row');
    if (!row) return;
    const id = Number(row.dataset.sessionId);
    if (e.target.closest('.session-delete')) deleteSession(id);
    else if (e.target.closest('.session-pick')) selectSession(id);
});

byId('sessions-more').addEventListener('click', () => loadSessions({ more: true }));

byId('sessions-select').addEventListener('change', (e) => {
    if (e.target.value === 'more') {
        e.target.value = String(selectedId);
        loadSessions({ more: true });
    } else {
        selectSession(Number(e.target.value));
    }
});

byId('session-delete-phone').addEventListener('click', () => {
    if (selectedId !== null) deleteSession(selectedId);
});

byId('shots-wrap').addEventListener('click', (e) => {
    const tr = e.target.closest('.shot-row');
    if (tr) toggleShot(tr);
});

byId('shots-wrap').addEventListener('keydown', (e) => {
    const tr = e.target.closest('.shot-row');
    if (tr && (e.key === 'Enter' || e.key === ' ')) {
        e.preventDefault();
        toggleShot(tr);
    }
});

window.addEventListener('resize', fitDetail);

openSocket('/ws', (msg) => {
    if (msg.result_type !== 'Hit' || msg.type) return;
    const before = selectedId;
    loadSessions().then(() => {
        if (before !== null && selectedId === before) loadShots();
    });
    loadStorageUsage();
});

loadStorageUsage();
loadSessions();
