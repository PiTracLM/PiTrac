// Dashboard: status strip, shot metrics, shot image, setup readiness
/* global openSocket, onPiTracStatus, formatNumber, api, escapeHtml, controlPiTrac */

// Keys are the exact result_type strings from parsers.py; anything else leaves the strip as it is
const STRIP_STATES = {
    'Initializing':                       ['initializing', 'Starting up', 'PiTrac is starting.'],
    'Waiting For Ball':                   ['waiting', 'Place a ball', 'Put a ball on the tee.'],
    'Waiting For Simulator':              ['waiting', 'Waiting for simulator', 'Connect a simulator to continue.'],
    'Waiting For Placement To Stabilize': ['stabilizing', 'Ball detected', 'Let the ball settle.'],
    'Ball Placed':                        ['ready', 'Ready. Hit it.', ''],
    'Hit':                                ['hit', 'Shot recorded', ''],
    'Multiple Balls Present':             ['error', 'More than one ball', 'Remove the extra balls.'],
    'Error':                              ['error', 'Error', ''],
};
const STRIP_CLASSES = ['initializing', 'waiting', 'stabilizing', 'ready', 'hit', 'error'];

let running = null;
let offline = false;
let shotState = null;
let setup = null;
let cameraLabels = {};
let sims = null;
let freshSocket = true;
let hitTime = '';
let shownImage = null;

const byId = (id) => document.getElementById(id);

function stripState() {
    if (running === null) return ['initializing', 'Checking PiTrac', ''];
    if (offline) return ['initializing', 'PiTrac is offline', "Can't reach the PiTrac web server."];
    if (!running) {
        const message = strobeBlocked() ? setup.strobe.reason || 'Finish setup before starting.' : 'Start it to begin.';
        return ['initializing', 'PiTrac is stopped', message];
    }
    return shotState || ['initializing', 'PiTrac is running', ''];
}

function strobeBlocked() {
    return !!setup && !setup.strobe.safe;
}

function setText(el, text) {
    if (el.textContent !== text) el.textContent = text;
}

function renderStrip() {
    const [state, title, message] = stripState();
    const stopped = running === false && !offline;
    const strip = byId('status-strip');
    if (!strip.classList.contains(state)) {
        strip.classList.remove(...STRIP_CLASSES);
        strip.classList.add(state);
    }
    setText(byId('status-strip-title'), title);
    setText(byId('status-strip-message'), message);
    document.querySelector('.status-strip-separator').hidden = !message;
    byId('strip-start-btn').hidden = !stopped || strobeBlocked();
    byId('strip-setup-btn').hidden = !stopped || !strobeBlocked();
    byId('btn-reset').hidden = !running || state !== 'hit';
}

function shotStateFor(data) {
    const def = STRIP_STATES[data.result_type];
    if (!def) return null;
    const [state, title, message] = def;
    if (state === 'hit') {
        hitTime = data.timestamp ? new Date(data.timestamp).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' }) : '';
        return [state, title, hitTime ? `Shot at ${hitTime}` : ''];
    }
    // The C++ sends Waiting For Ball right after every Hit; keep the shot up until the next ball shows
    if (data.result_type === 'Waiting For Ball' && shotState && shotState[0] === 'hit') {
        return ['hit', shotState[1], hitTime ? `Shot at ${hitTime}. Place the next ball.` : 'Place the next ball.'];
    }
    return [state, title, message || (state === 'error' ? data.message || '' : '')];
}

// -- Metrics --

function setMetric(id, text, dir = '') {
    const el = byId(id);
    const value = el.querySelector('.metric-value');
    el.querySelector('.metric-dir').textContent = dir ? ` ${dir}` : '';
    if (value.textContent === text) return;
    value.textContent = text;
    el.classList.add('updated');
    setTimeout(() => el.classList.remove('updated'), 500);
}

// Negative is left, the sign the C++ sends and the sims pass straight through to GSPro HLA and SideSpin
function setSidedMetric(id, value, decimals) {
    const text = formatNumber(value, decimals);
    if (text === '--') {
        setMetric(id, text);
        return;
    }
    const n = Number(text) || 0;
    setMetric(id, Math.abs(n).toFixed(decimals), n < 0 ? 'L' : n > 0 ? 'R' : '');
}

function renderMetrics(data) {
    setMetric('speed', formatNumber(data.speed, 1));
    setMetric('launch_angle', formatNumber(data.launch_angle, 1));
    setSidedMetric('side_angle', data.side_angle, 1);
    setMetric('back_spin', formatNumber(data.back_spin, 0));
    setSidedMetric('side_spin', data.side_spin, 0);
}

// -- Image panel --

function renderEmptyImage(text) {
    const inner = byId('image-panel-inner');
    inner.innerHTML = '<div class="image-empty-state"><div class="empty-icon"></div><div class="empty-text"></div></div>';
    inner.querySelector('.empty-text').textContent = text;
}

function renderImage(path) {
    const img = document.createElement('img');
    img.src = `/images/${encodeURI(path)}?t=${Date.now()}`;
    img.alt = 'Shot image';
    img.className = 'shot-image';
    img.addEventListener('click', () => window.open(`/images/${encodeURI(path)}`, '_blank'));
    byId('image-panel-inner').replaceChildren(img);
    shownImage = path;
}

const shotDir = (path) => path.slice(0, path.lastIndexOf('/'));

function renderStoredShot(shot) {
    renderMetrics(shot);
    if (shot.images && shot.images.length) renderImage(shot.images[0]);
    else renderEmptyImage('Hit a shot to see the image here');
}

// -- Socket --

function onMessage(data) {
    if (data.type === 'image_ready') {
        renderImage(data.filename);
        return;
    }
    if (!('result_type' in data) || data.type) return;
    renderMetrics(data);
    if (freshSocket) {
        freshSocket = false;
        const stored = data.images && data.images[0];
        if (stored && !(shownImage && shotDir(shownImage) === shotDir(stored))) renderImage(stored);
    }
    const next = shotStateFor(data);
    if (next) {
        shotState = next;
        renderStrip();
    }
}

async function resetShot() {
    try {
        await api('/api/reset', { method: 'POST' });
        renderEmptyImage('Waiting for shot...');
        shotState = null;
        renderStrip();
    } catch (err) {
        console.error('Error resetting shot:', err);
    }
}

// -- Simulators --

function connectedSims() {
    if (sims) return sims.filter((s) => s.status === 'connected').map((s) => s.display_name || s.name);
    return setup ? setup.simulator.connected : [];
}

function renderSimChip() {
    const names = connectedSims();
    byId('sim-chip-text').textContent = names.length ? `${names.join(', ')} connected` : 'No simulator connected';
    byId('sim-chip-dot').className = `w-2 h-2 rounded-full ${names.length ? 'bg-success' : 'bg-base-content/30'}`;
    byId('sim-chip').hidden = false;
}

function openSimsDrawer() {
    const drawer = byId('sims-drawer');
    if (drawer && drawer.classList.contains('hidden')) byId('sims-nav-btn').click();
}

// -- Setup readiness --

function simulatorRow() {
    const enabled = sims ? sims.map((s) => s.display_name || s.name) : setup.simulator.enabled;
    const connected = connectedSims();
    if (connected.length) return { label: 'Simulator', detail: `${connected.join(', ')} connected`, done: true };
    if (enabled.length) {
        return { label: 'Simulator', detail: `${enabled.join(', ')} is set up but not connected.`, action: 'Open Sims', onClick: true };
    }
    return { label: 'Simulator', detail: 'Pick GSPro, E6, or OpenGolfSim.', action: 'Set up', href: '/config#setup' };
}

function setupRows() {
    const camera = (n) => cameraLabels[setup.cameras[`camera${n}`].type] || setup.cameras[`camera${n}`].type || 'not set';
    const rows = [{
        label: 'Hardware',
        detail: `${setup.board_version ? `V${setup.board_version} board` : 'Board not set'}, camera 1 ${camera(1)}, camera 2 ${camera(2)}`,
        action: 'Review',
        href: '/config#setup',
    }];
    if (setup.strobe.required) {
        rows.push({ label: 'Strobe', detail: setup.strobe.safe ? '' : setup.strobe.reason || 'Needed before PiTrac can start.', done: setup.strobe.safe, action: 'Calibrate', href: '/calibration#strobe' });
    }
    for (const [kind, title] of [['lens', 'Lens'], ['position', 'Position']]) {
        for (const n of [1, 2]) {
            const done = setup.cameras[`camera${n}`][`${kind}_calibrated`];
            rows.push({ label: `${title}, camera ${n}`, detail: '', done, action: 'Calibrate', href: '/calibration' });
        }
    }
    rows.push(simulatorRow());
    return rows;
}

function rowHtml(row) {
    const status = row.done
        ? '<span class="flex items-center gap-1 text-success text-sm"><i data-lucide="circle-check" class="icon-sm"></i>Done</span>'
        : row.href
            ? `<a class="btn btn-sm" href="${escapeHtml(row.href)}">${escapeHtml(row.action)}</a>`
            : `<button class="btn btn-sm" data-open-sims>${escapeHtml(row.action)}</button>`;
    return `
        <li class="list-row items-center">
            <i data-lucide="${row.done ? 'circle-check' : 'circle-dashed'}" class="icon-sm ${row.done ? 'text-success' : 'opacity-50'}"></i>
            <div class="min-w-0">
                <div class="font-medium">${escapeHtml(row.label)}</div>
                ${row.detail ? `<div class="text-sm opacity-70 break-words">${escapeHtml(row.detail)}</div>` : ''}
            </div>
            ${status}
        </li>`;
}

function renderSetup() {
    const card = byId('setup-card');
    card.hidden = !setup || setup.complete;
    if (card.hidden) return;
    const list = byId('setup-rows');
    list.innerHTML = setupRows().map(rowHtml).join('');
    if (typeof lucide !== 'undefined') lucide.createIcons({ nodes: [list] });
}

async function loadSetup() {
    try {
        setup = await api('/api/setup/status');
    } catch (err) {
        console.error('Could not load setup status:', err);
        return;
    }
    renderStrip();
    if (!sims) renderSimChip();
    if (setup.complete) return;
    try {
        const meta = await api('/api/config/metadata');
        const options = (meta['cameras.slot1.type'] || {}).options || {};
        cameraLabels = Object.fromEntries(Object.entries(options).map(([value, label]) => [value, label.split(' - ')[0]]));
    } catch (err) {
        console.error('Could not load camera labels:', err);
    }
    renderSetup();
}

// -- Init --

onPiTracStatus((s) => {
    const now = s.is_running && !s.offline;
    if (running && !now) shotState = null;
    running = now;
    offline = s.offline;
    renderStrip();
});

document.addEventListener('pitrac:sims', (e) => {
    sims = e.detail;
    renderSimChip();
    if (setup) renderSetup();
});

renderStoredShot(JSON.parse(byId('initial-shot').textContent));
renderStrip();

byId('strip-start-btn').addEventListener('click', () => controlPiTrac('start'));
byId('btn-reset').addEventListener('click', resetShot);
byId('sim-chip').addEventListener('click', openSimsDrawer);
byId('setup-rows').addEventListener('click', (e) => {
    if (e.target.closest('[data-open-sims]')) openSimsDrawer();
});

openSocket('/ws', onMessage, { onOpen: () => { freshSocket = true; } });
loadSetup();
