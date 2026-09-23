// Setup checklist rules and renderer, shared by the dashboard card and the calibration page
/* global escapeHtml, api */
/* exported SETUP_CAMERAS, setupLock, setupChecklist, renderSetupChecklist, setupSummaryHtml, loadCameraLabels */

const SETUP_CAMERAS = ['camera1', 'camera2'];
const SETUP_STEPS = [
    { step: 'strobe', hash: 'strobe', title: 'Strobe', hint: 'Redo if you change the LED board or power supply.' },
    { step: 'lens', hash: 'lens', title: 'Lens calibration', hint: 'Redo if you swap or refocus the lens or replace the camera.' },
    { step: 'position', hash: 'ball', title: 'Ball calibration', hint: 'Redo if PiTrac or a camera moves or gets bumped.' },
];

const LOCKED = ['strobe', 'lens', 'unknown'];
const LINE_LOOK = {
    done: ['circle-check', 'text-success', 'text-base-content/70'],
    stale: ['circle-alert', 'text-warning', 'text-warning'],
    todo: ['circle-dashed', 'text-base-content/50', 'text-base-content/70'],
    running: ['loader-circle', 'text-info animate-spin', 'text-info'],
    locked: ['lock', 'text-base-content/40', 'text-base-content/50'],
};
const LINE_TEXT = {
    stale: 'Redo recommended',
    todo: 'Needs calibration',
    running: 'Running',
    strobe: 'Do the strobe first',
    lens: 'Do the lens first',
    unknown: 'Status unavailable',
};
const ITEM_LOOK = {
    done: ['circle-check', 'bg-success/20 text-success'],
    partial: ['circle-dot-dashed', 'bg-primary/20 text-primary'],
    stale: ['circle-alert', 'bg-warning/20 text-warning'],
    next: ['circle-dashed', 'bg-primary/20 text-primary'],
    todo: ['circle-dashed', 'bg-base-300 text-base-content/60'],
    running: ['loader-circle', 'bg-info/20 text-info'],
    locked: ['lock', 'bg-base-300 text-base-content/40'],
};
const cameraName = (camera) => `Camera ${camera.slice(-1)}`;

function shortDate(iso) {
    const date = new Date(iso);
    if (!iso || Number.isNaN(date.getTime())) return '';
    const sameYear = date.getFullYear() === new Date().getFullYear();
    return date.toLocaleDateString(undefined, { month: 'short', day: 'numeric', year: sameYear ? undefined : 'numeric' });
}

// Strict order: strobe, then the lens on a camera, then the ball calibration on that camera
function setupLock(setup, step, camera) {
    if (step === 'strobe') return null;
    if (!setup) return 'unknown';
    if (setup.strobe.required && !setup.strobe.safe) return 'strobe';
    if (step === 'position' && !setup.cameras[camera].lens_calibrated) return 'lens';
    return null;
}

function lineState(setup, step, camera, running) {
    if (running) return 'running';
    if (step === 'strobe') return !setup ? 'unknown' : setup.strobe.safe ? 'done' : 'todo';
    const lock = setupLock(setup, step, camera);
    if (lock) return lock;
    const cam = setup.cameras[camera];
    if (!cam[`${step}_calibrated`]) return 'todo';
    const lensNewer = step === 'position' && cam.lens_updated_at && cam.position_updated_at
        && new Date(cam.lens_updated_at) > new Date(cam.position_updated_at);
    return lensNewer ? 'stale' : 'done';
}

function lineText(setup, step, camera, state) {
    if (state === 'done') {
        const date = shortDate(step === 'strobe' ? setup.strobe.updated_at : setup.cameras[camera][`${step}_updated_at`]);
        return date ? `Done, ${date}` : 'Done';
    }
    const reason = setup?.strobe.reason || '';
    if (step === 'strobe' && state === 'todo' && reason && !reason.includes('requires strobe calibration')) return reason;
    return LINE_TEXT[state];
}

function itemState(states, isNext) {
    if (states.includes('running')) return 'running';
    if (states.every(s => s === 'done')) return 'done';
    if (states.includes('stale')) return 'stale';
    if (states.includes('done')) return 'partial';
    if (states.includes('todo')) return isNext ? 'next' : 'todo';
    return 'locked';
}

// running(step, camera) and failures (keyed "strobe" or "<step>-<camera>") come from the calibration page
function setupChecklist(setup, { running = () => false, failures = {} } = {}) {
    let primaryTaken = false;
    return SETUP_STEPS.filter(def => def.step !== 'strobe' || setup?.strobe.required).map(def => {
        const lines = (def.step === 'strobe' ? [''] : SETUP_CAMERAS).map(camera => {
            const state = lineState(setup, def.step, camera, running(def.step, camera));
            const failure = state === 'running' ? '' : failures[camera ? `${def.step}-${camera}` : def.step];
            return {
                camera,
                state,
                text: lineText(setup, def.step, camera, state),
                note: failure || (state === 'stale' ? 'The lens was recalibrated after this.' : ''),
                noteClass: failure ? 'text-error' : 'text-warning',
            };
        });
        const live = lines.find(l => l.state === 'running');
        const next = lines.find(l => l.state === 'todo' || l.state === 'stale');
        const done = lines.filter(l => l.state === 'done').map(l => l.camera);
        let action = null;
        if (live) {
            action = { kind: 'go', camera: live.camera, label: 'View' };
        } else if (next) {
            const verb = next.state === 'stale' ? 'Redo' : 'Calibrate';
            action = { kind: 'go', camera: next.camera, label: next.camera ? `${verb} ${cameraName(next.camera).toLowerCase()}` : verb, primary: !primaryTaken };
            primaryTaken = true;
        } else if (done.length) {
            action = { kind: 'redo', cameras: done };
        }
        return { ...def, lines, action, state: itemState(lines.map(l => l.state), action?.primary) };
    });
}

const icon = (name, classes) => `<i data-lucide="${name}" class="icon-sm ${classes}"></i>`;

function lineHtml(line) {
    const [name, iconClass, textClass] = LINE_LOOK[LOCKED.includes(line.state) ? 'locked' : line.state];
    const label = line.camera ? `<span class="font-medium">${cameraName(line.camera)}</span> <span class="text-base-content/40">&middot;</span> ` : '';
    const note = line.note ? `<span class="block text-xs mt-0.5 ${line.noteClass}">${escapeHtml(line.note)}</span>` : '';
    return `
        <li class="flex items-start gap-1.5">
            ${icon(name, `shrink-0 mt-0.5 ${iconClass}`)}
            <span class="min-w-0">${label}<span class="${textClass}">${escapeHtml(line.text)}</span>${note}</span>
        </li>`;
}

function actionHtml(item, base) {
    const { action } = item;
    if (!action) return '';
    const href = (camera) => escapeHtml(`${base}#${item.hash}${camera ? `-${camera}` : ''}`);
    if (action.kind === 'go') {
        const style = action.primary ? 'btn-primary' : action.label === 'View' ? 'btn-ghost' : '';
        return `<a class="btn btn-sm ${style}" href="${href(action.camera)}">${escapeHtml(action.label)}</a>`;
    }
    if (action.cameras.length === 1 && !action.cameras[0]) {
        return `<a class="btn btn-sm btn-ghost" href="${href('')}">Redo</a>`;
    }
    return `
        <details class="setup-redo dropdown sm:dropdown-end">
            <summary class="btn btn-sm btn-ghost gap-1" aria-label="Redo ${escapeHtml(item.title.toLowerCase())}">Redo ${icon('chevron-down', 'opacity-60')}</summary>
            <ul class="menu dropdown-content bg-base-100 rounded-box z-20 w-40 p-1 mt-1 shadow-lg border border-base-300">
                ${action.cameras.map(c => `<li><a href="${href(c)}">${cameraName(c)}</a></li>`).join('')}
            </ul>
        </details>`;
}

function itemHtml(item, { base, hints }) {
    const [name, tile] = ITEM_LOOK[item.state];
    const spin = item.state === 'running' ? 'animate-spin' : '';
    return `
        <li class="list-row items-start gap-3 sm:gap-4 px-2 py-3 sm:p-4">
            <div class="w-9 h-9 rounded-full grid place-items-center ${tile}">${icon(name, spin)}</div>
            <div class="min-w-0">
                <div class="font-semibold leading-9">${escapeHtml(item.title)}</div>
                <ul class="text-sm space-y-1">${item.lines.map(lineHtml).join('')}</ul>
                ${hints ? `<div class="text-xs text-base-content/60 mt-2">${escapeHtml(item.hint)}</div>` : ''}
            </div>
            <div class="setup-action sm:self-center">${actionHtml(item, base)}</div>
        </li>`;
}

const renderedChecklists = new WeakMap();

// base is prepended to the section links: "/calibration" from other pages, "" on the calibration page itself
function renderSetupChecklist(list, items, { base = '', hints = false } = {}) {
    const html = items.map(item => itemHtml(item, { base, hints })).join('');
    if (renderedChecklists.get(list) === html) return;
    renderedChecklists.set(list, html);
    list.innerHTML = html;
    if (typeof lucide !== 'undefined') lucide.createIcons({ nodes: [list] });
}

function setupSummaryHtml(setup, cameraLabels) {
    const camera = (n) => {
        const type = setup.cameras[`camera${n}`].type;
        return cameraLabels[type] || type || 'Not set';
    };
    const pairs = [
        ['Board', setup.board_version ? `V${setup.board_version}` : 'Not set'],
        ['Camera 1', camera(1)],
        ['Camera 2', camera(2)],
    ];
    return pairs.map(([label, value], i) => `<span class="whitespace-nowrap"><span class="text-base-content/60">${label}:</span> <span class="font-semibold">${escapeHtml(value)}</span>${i < pairs.length - 1 ? ' <span class="text-base-content/40">&middot;</span>' : ''}</span>`).join(' ')
        + ' <a href="/config#setup" class="link link-primary whitespace-nowrap ml-1">Change</a>';
}

async function loadCameraLabels() {
    try {
        const meta = await api('/api/config/metadata');
        const options = (meta['cameras.slot1.type'] || {}).options || {};
        return Object.fromEntries(Object.entries(options).map(([value, label]) => [value, label.split(' - ')[0]]));
    } catch (err) {
        console.error('Could not load camera labels:', err);
        return {};
    }
}

document.addEventListener('click', (e) => {
    document.querySelectorAll('details.setup-redo[open]').forEach(menu => {
        if (!menu.contains(e.target) || e.target.closest('a')) menu.open = false;
    });
});
