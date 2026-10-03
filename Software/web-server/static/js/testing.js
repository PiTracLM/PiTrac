/* global requireStrobeSafe, escapeHtml, toast, api, onPiTracStatus */

const STATES = {
    idle: ['bg-base-content/20', 'Not run'],
    running: ['bg-warning animate-pulse', 'Running'],
    success: ['bg-success', 'Passed'],
    failed: ['bg-error', 'Failed'],
    timeout: ['bg-warning', 'Timed out'],
    stopped: ['bg-base-content/40', 'Stopped'],
    error: ['bg-error', 'Could not run'],
};

const tools = {};
const results = {};
const running = new Map();
let pitracRunning = false;
let pollTimer = null;

const byId = (id) => document.getElementById(id);
const stateOf = (status) => STATES[status] || STATES.error;

function textEl(tag, text, className = '') {
    const el = document.createElement(tag);
    el.className = className;
    el.textContent = text;
    return el;
}

async function loadTools() {
    try {
        const data = await api('/api/testing/tools');
        Object.entries(data).forEach(([category, list]) => {
            list.forEach(tool => {
                tools[tool.id] = tool;
                const container = byId(tool.id === 'test_uploaded_image' ? 'upload-tools' : `${category}-tools`);
                if (container) container.appendChild(createToolCard(tool));
            });
        });
    } catch (err) {
        toast(`Could not load the testing tools: ${err.message}`, 'error');
        return;
    }
    await pollStatus(false);
}

function createToolCard(tool) {
    const card = document.createElement('div');
    card.className = 'tool-card flex flex-col gap-2';
    card.dataset.toolId = tool.id;
    card.innerHTML = `
        <div class="flex items-start justify-between gap-2">
            <h3 class="tool-name">${escapeHtml(tool.name)}</h3>
            <span class="tool-state flex items-center gap-1.5 text-xs whitespace-nowrap pt-0.5"></span>
        </div>
        <p class="tool-description grow-0">${escapeHtml(tool.description)}</p>
        <dl class="text-xs grid grid-cols-[auto_1fr] gap-x-2 gap-y-1">
            <dt class="opacity-60">Before</dt><dd>${escapeHtml(tool.before)}</dd>
            <dt class="opacity-60">Expect</dt><dd>${escapeHtml(tool.success)}</dd>
        </dl>
        <p class="tool-message grow-0 text-xs text-error hidden"></p>
        <div class="tool-actions mt-auto pt-2">
            <button class="btn btn-primary btn-sm run-btn">Run</button>
            <button class="btn btn-error btn-sm stop-btn hidden">Stop</button>
            <button class="btn btn-ghost btn-sm view-btn hidden">View output</button>
        </div>`;
    card.querySelector('.run-btn').addEventListener('click', () => runTool(tool.id));
    card.querySelector('.stop-btn').addEventListener('click', () => stopTool(tool.id));
    card.querySelector('.view-btn').addEventListener('click', () => showOutput(tool.id));
    return card;
}

function renderCard(card) {
    const id = card.dataset.toolId;
    const isRunning = running.has(id);
    const result = results[id];
    const status = isRunning ? 'running' : result ? result.status : 'idle';
    const [dot, word] = stateOf(status);
    const label = isRunning ? `${word} ${Math.max(0, Math.round((Date.now() - running.get(id)) / 1000))}s` : word;
    const stateEl = card.querySelector('.tool-state');
    if (stateEl.textContent !== label) {
        stateEl.replaceChildren(textEl('span', '', `w-2 h-2 rounded-full ${dot}`), textEl('span', label));
    }
    card.classList.toggle('running', isRunning);

    const message = card.querySelector('.tool-message');
    const showMessage = !isRunning && result && ['error', 'timeout'].includes(result.status) && result.message;
    message.textContent = showMessage ? result.message : '';
    message.classList.toggle('hidden', !showMessage);

    const runBtn = card.querySelector('.run-btn');
    runBtn.classList.toggle('hidden', isRunning);
    runBtn.disabled = pitracRunning || running.size > 0;
    runBtn.title = pitracRunning ? 'Stop PiTrac to run a test' : running.size > 0 ? 'Another test is running' : '';
    card.querySelector('.stop-btn').classList.toggle('hidden', !isRunning);
    card.querySelector('.view-btn').classList.toggle('hidden', isRunning || !result);
}

function renderAll() {
    document.querySelectorAll('.tool-card').forEach(renderCard);
    byId('pitrac-running-note').classList.toggle('hidden', !pitracRunning);
}

async function runTool(toolId) {
    if (!(await requireStrobeSafe())) return;

    running.set(toolId, Date.now());
    delete results[toolId];
    renderAll();

    let response;
    try {
        response = await api(`/api/testing/run/${toolId}`, { method: 'POST' });
    } catch (err) {
        response = { status: 'error', message: err.message };
    }
    if (response.status === 'started') startPolling();
    else finishTool(toolId, response, true);
}

async function stopTool(toolId) {
    try {
        const response = await api(`/api/testing/stop/${toolId}`, { method: 'POST' });
        if (response.status !== 'success') toast(response.message, 'error');
    } catch (err) {
        toast(`Could not stop the test: ${err.message}`, 'error');
    }
    await pollStatus();
}

function finishTool(toolId, result, live) {
    running.delete(toolId);
    results[toolId] = result;
    renderAll();
    if (!live) return;

    const name = tools[toolId]?.name || toolId;
    if (result.status === 'success') {
        if (result.image_url) showOutput(toolId);
        else toast(`${name} passed`, 'success');
    } else if (result.status === 'error') {
        toast(result.message || `${name} could not run`, 'error');
    } else if (result.status !== 'stopped') {
        toast(`${name}: ${stateOf(result.status)[1].toLowerCase()}`, 'error', {
            actionLabel: 'View output',
            onAction: () => showOutput(toolId),
        });
    }
}

async function pollStatus(live = true) {
    let data;
    try {
        data = await api('/api/testing/status');
    } catch (err) {
        console.error('Failed to poll testing status:', err);
        return;
    }
    const serverRunning = new Set(data.running || []);
    const serverResults = data.results || {};

    if (!live) Object.assign(results, serverResults);
    serverRunning.forEach(id => {
        if (!running.has(id)) running.set(id, Date.now() - ((data.elapsed || {})[id] || 0) * 1000);
    });
    for (const id of [...running.keys()]) {
        if (serverRunning.has(id)) continue;
        if (serverResults[id]) finishTool(id, serverResults[id], live);
        else running.delete(id);
    }
    renderAll();
    if (running.size) startPolling();
    else stopPolling();
}

function startPolling() {
    if (pollTimer) return;
    let tick = 0;
    pollTimer = setInterval(() => {
        renderAll();
        if (++tick % 2 === 0) pollStatus();
    }, 1000);
}

function stopPolling() {
    clearInterval(pollTimer);
    pollTimer = null;
}

function showOutput(toolId) {
    const result = results[toolId];
    if (!result) return;
    const name = tools[toolId]?.name || toolId;
    byId('modalTitle').textContent = `${name}: ${stateOf(result.status)[1].toLowerCase()}`;

    const body = byId('modalBody');
    body.replaceChildren();
    if (result.message) body.append(textEl('p', result.message, 'text-sm'));
    if (result.image_url) {
        const src = `${result.image_url}?t=${encodeURIComponent(result.timestamp || Date.now())}`;
        const img = document.createElement('img');
        img.src = src;
        img.alt = `${name} picture`;
        img.className = 'w-full h-auto rounded-lg';
        const download = textEl('a', 'Download', 'btn btn-sm btn-primary self-center');
        download.href = src;
        download.download = '';
        body.append(img, download);
    }
    const section = (label, text) => body.append(
        textEl('div', label, 'text-xs font-semibold uppercase tracking-wide opacity-60'),
        textEl('pre', text, 'terminal max-h-[50vh] m-0'),
    );
    if (result.error) section('Error output', result.error);
    if (result.output) section('Output', result.output);
    if (!body.childElementCount) body.append(textEl('p', 'No output.', 'text-sm opacity-60'));
    byId('testModal').showModal();
}

// -- Image upload --

function setupImageUpload() {
    const uploadArea = byId('uploadArea');
    const fileInput = byId('imageUpload');

    uploadArea.addEventListener('click', () => fileInput.click());
    uploadArea.addEventListener('keydown', (e) => {
        if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
            fileInput.click();
        }
    });
    fileInput.addEventListener('change', () => handleFile(fileInput.files[0]));
    byId('clearImageBtn').addEventListener('click', clearImage);

    uploadArea.addEventListener('dragover', (e) => {
        e.preventDefault();
        uploadArea.classList.add('drag-over');
    });
    uploadArea.addEventListener('dragleave', () => uploadArea.classList.remove('drag-over'));
    uploadArea.addEventListener('drop', (e) => {
        e.preventDefault();
        uploadArea.classList.remove('drag-over');
        handleFile(e.dataTransfer.files[0]);
    });
}

async function handleFile(file) {
    if (!file) return;
    if (!file.type.startsWith('image/')) {
        toast('Choose an image file.', 'error');
        return;
    }

    const reader = new FileReader();
    reader.onload = (e) => {
        byId('previewImg').src = e.target.result;
        byId('imageName').textContent = file.name;
        byId('uploadArea').classList.add('hidden');
        byId('imagePreview').classList.remove('hidden');
    };
    reader.readAsDataURL(file);

    const formData = new FormData();
    formData.append('file', file);
    try {
        const response = await fetch('/api/testing/upload-image', { method: 'POST', body: formData });
        const result = await response.json();
        if (result.status !== 'success') throw new Error(result.message);
        toast(`Uploaded ${result.filename}`, 'success');
    } catch (err) {
        toast(`Upload failed: ${err.message}`, 'error');
        clearImage();
    }
}

function clearImage() {
    byId('uploadArea').classList.remove('hidden');
    byId('imagePreview').classList.add('hidden');
    byId('imageUpload').value = '';
}

onPiTracStatus((s) => {
    const now = s.is_running && !s.offline;
    if (now === pitracRunning) return;
    pitracRunning = now;
    renderAll();
});

setupImageUpload();
loadTools();
