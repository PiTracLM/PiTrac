// sims.js - navbar simulator menu (live status over /ws/sims) and the add/edit simulator dialog
/* global api, toast, escapeHtml, openSocket, confirmDialog */
(function () {
    const STATUS = {
        connected: { label: 'Connected', dot: 'bg-success' },
        connecting: { label: 'Connecting', dot: 'bg-warning' },
        error: { label: "Can't connect", dot: 'bg-error' },
        off: { label: 'Not connected', dot: 'bg-base-content/30' },
    };

    let sims = [];
    let typesPromise = null;
    let editing = null;

    const byId = (id) => document.getElementById(id);

    function statusOf(s) {
        if (!s.on) return { label: 'Off', dot: 'bg-base-content/20' };
        return STATUS[s.status] || STATUS.off;
    }

    // The order is the precedence: anything connected wins, then connecting, then errors
    function summary() {
        const on = sims.filter((s) => s.on);
        const connected = on.filter((s) => s.status === 'connected');
        if (connected.length === 1) return { label: `${connected[0].name} connected`, dot: STATUS.connected.dot };
        if (connected.length > 1) return { label: `${connected.length} simulators connected`, dot: STATUS.connected.dot };
        if (on.some((s) => s.status === 'connecting')) return { label: 'Connecting', dot: STATUS.connecting.dot };
        if (on.some((s) => s.status === 'error')) return { label: "Can't connect", dot: STATUS.error.dot };
        if (on.length) return { label: 'Not connected', dot: STATUS.off.dot };
        return { label: 'Simulator', dot: null };
    }

    function rowHtml(s) {
        const st = statusOf(s);
        const id = escapeHtml(s.id);
        const where = [s.display_type, s.target].filter(Boolean).join(' · ');
        let action = '';
        if (s.on) {
            const connecting = s.status === 'connecting';
            const verb = s.status === 'connected' ? 'disconnect' : 'connect';
            const label = connecting ? 'Connecting' : s.status === 'connected' ? 'Disconnect' : 'Connect';
            action = `<button type="button" class="btn btn-xs" data-id="${id}" data-action="${verb}" ${connecting ? 'disabled' : ''}>${label}</button>`;
        }
        const detail = s.on && s.status === 'error' && s.detail
            ? `<div class="text-xs text-error break-words mt-1">${escapeHtml(s.detail)}</div>` : '';
        return `
            <li class="border border-base-300 rounded-box p-2 mb-2 bg-base-100">
                <div class="flex items-start gap-2">
                    <div class="min-w-0 flex-1">
                        <div class="font-medium truncate">${escapeHtml(s.name)}</div>
                        <div class="text-xs opacity-60 truncate">${escapeHtml(where)}</div>
                        <div class="flex items-center gap-1.5 text-xs mt-1">
                            <span class="inline-block w-2 h-2 rounded-full ${st.dot}"></span>${st.label}
                        </div>
                        ${detail}
                    </div>
                    <input type="checkbox" class="toggle toggle-sm mt-0.5" data-id="${id}" data-action="toggle"
                           aria-label="${escapeHtml(s.name)} on" ${s.on ? 'checked' : ''}>
                    <details class="dropdown dropdown-end">
                        <summary class="btn btn-ghost btn-xs btn-square" aria-label="More for ${escapeHtml(s.name)}">
                            <i data-lucide="ellipsis-vertical" class="icon-sm"></i>
                        </summary>
                        <ul class="menu dropdown-content bg-base-200 rounded-box z-50 w-32 p-1 shadow-xl border border-base-300">
                            <li><button type="button" data-id="${id}" data-action="edit"><i data-lucide="pencil" class="icon-sm"></i>Edit</button></li>
                            <li><button type="button" class="text-error" data-id="${id}" data-action="delete"><i data-lucide="trash-2" class="icon-sm"></i>Delete</button></li>
                        </ul>
                    </details>
                </div>
                ${action ? `<div class="flex justify-end mt-2">${action}</div>` : ''}
            </li>`;
    }

    function render(list) {
        sims = list;
        const { label, dot } = summary();
        const dotEl = byId('sims-status-dot');
        if (dotEl) {
            dotEl.className = `w-2 h-2 rounded-full ${dot || 'hidden'}`;
            byId('sims-nav-label').textContent = label;
            byId('sims-nav-btn').setAttribute('aria-label', label);
        }
        const el = byId('sims-list');
        if (!el) return;
        el.innerHTML = sims.length
            ? `<ul>${sims.map(rowHtml).join('')}</ul>`
            : '<p class="text-sm opacity-70 p-2">No simulators yet. Simulators are optional.</p>';
        if (typeof lucide !== 'undefined') lucide.createIcons({ nodes: [el] });
    }

    async function refresh() {
        try {
            render(await api('/api/sims'));
        } catch (err) {
            console.error('Could not load simulators:', err);
        }
    }

    function closeMenu() {
        const menu = byId('sims-menu');
        if (menu) menu.open = false;
    }

    async function onListClick(e) {
        const el = e.target.closest('[data-action]');
        if (!el) return;
        const sim = sims.find((s) => s.id === el.dataset.id);
        if (!sim) return;
        const action = el.dataset.action;
        if (action === 'edit') {
            closeMenu();
            openSimulatorEditor(sim.id);
            return;
        }
        if (action === 'delete') {
            closeMenu();
            const ok = await confirmDialog({
                title: `Delete ${sim.name}?`,
                body: 'PiTrac stops sending shots to it. You can add it again later.',
                confirmLabel: 'Delete',
                danger: true,
            });
            if (!ok) return;
            try {
                await api(`/api/sims/${encodeURIComponent(sim.id)}`, { method: 'DELETE' });
                toast(`${sim.name} deleted`, 'success');
            } catch (err) {
                toast(err.message, 'error');
            }
            refresh();
            return;
        }
        if (action === 'toggle') {
            el.disabled = true;
            try {
                await api(`/api/sims/${encodeURIComponent(sim.id)}`, { method: 'PUT', body: { on: el.checked } });
            } catch (err) {
                el.checked = !el.checked;
                toast(err.message, 'error');
            }
            el.disabled = false;
            refresh();
            return;
        }
        el.disabled = true;
        try {
            render(await api(`/api/sims/${encodeURIComponent(sim.id)}/${action}`, { method: 'POST' }));
        } catch (err) {
            toast(err.message, 'error');
            el.disabled = false;
        }
    }

    // -- Editor dialog --

    function loadTypes() {
        if (!typesPromise) {
            typesPromise = api('/api/sims/types').catch((err) => {
                typesPromise = null;
                throw err;
            });
        }
        return typesPromise;
    }

    function fieldHtml(field, value) {
        const key = escapeHtml(field.key);
        const attrs = field.type === 'integer'
            ? `type="number" inputmode="numeric" step="1" min="${field.min}" max="${field.max}"`
            : 'type="text" autocomplete="off" autocapitalize="off" spellcheck="false" placeholder="192.168.1.20"';
        return `
            <fieldset class="fieldset">
                <legend class="fieldset-legend">${escapeHtml(field.label)}</legend>
                <input class="input w-full" name="${key}" ${attrs} value="${escapeHtml(value ?? '')}">
                <p class="label text-error whitespace-normal hidden" data-error-for="${key}"></p>
            </fieldset>`;
    }

    function formHtml(type, sim) {
        const settings = sim ? sim.settings : {};
        const value = (f) => (f.key in settings ? settings[f.key] : f.default);
        const basic = type.fields.filter((f) => !f.advanced);
        const advanced = type.fields.filter((f) => f.advanced);
        return `
            <fieldset class="fieldset">
                <legend class="fieldset-legend">Name</legend>
                <input class="input w-full" name="name" type="text" maxlength="60"
                       placeholder="${escapeHtml(type.display_name)}" value="${escapeHtml(sim ? sim.name : '')}">
                <p class="label text-error whitespace-normal hidden" data-error-for="name"></p>
            </fieldset>
            ${basic.map((f) => fieldHtml(f, value(f))).join('')}
            ${advanced.length ? `
                <details class="mt-2">
                    <summary class="cursor-pointer text-sm opacity-70">Advanced</summary>
                    ${advanced.map((f) => fieldHtml(f, value(f))).join('')}
                </details>` : ''}
            <label class="flex items-center justify-between gap-3 mt-4">
                <span>
                    <span class="font-medium">On</span>
                    <span class="block text-xs opacity-60">Connect when PiTrac starts and keep reconnecting.</span>
                </span>
                <input type="checkbox" class="toggle" name="on" ${!sim || sim.on ? 'checked' : ''}>
            </label>`;
    }

    function showStep(step) {
        byId('sim-editor-types').classList.toggle('hidden', step !== 'types');
        byId('sim-editor-form').classList.toggle('hidden', step !== 'form');
    }

    function showForm(type, sim) {
        editing = { type, sim };
        byId('sim-editor-title').textContent = sim ? `Edit ${sim.name}` : `Add ${type.display_name}`;
        byId('sim-editor-fields').innerHTML = formHtml(type, sim);
        byId('sim-editor-result').innerHTML = '';
        showStep('form');
        byId('sim-editor-fields').querySelector('input[name="name"]').focus();
    }

    function readForm() {
        const form = byId('sim-editor-form');
        const settings = {};
        for (const field of editing.type.fields) {
            const input = form.elements[field.key];
            settings[field.key] = field.type === 'integer' && input.value !== '' ? Number(input.value) : input.value;
        }
        return { name: form.elements.name.value, on: form.elements.on.checked, settings };
    }

    function showErrors(fields) {
        const form = byId('sim-editor-form');
        form.querySelectorAll('[data-error-for]').forEach((p) => {
            const message = fields && fields[p.dataset.errorFor];
            p.textContent = message || '';
            p.classList.toggle('hidden', !message);
            const input = form.elements[p.dataset.errorFor];
            if (input) {
                input.classList.toggle('input-error', !!message);
                input.setAttribute('aria-invalid', message ? 'true' : 'false');
            }
        });
        const first = form.querySelector('.input-error');
        if (first) {
            first.closest('details')?.setAttribute('open', '');
            first.focus();
        }
    }

    function setBusy(btn, busy) {
        btn.disabled = busy;
        btn.querySelector('.loading')?.remove();
        if (busy) {
            const spinner = document.createElement('span');
            spinner.className = 'loading loading-spinner loading-xs';
            btn.prepend(spinner);
        }
    }

    async function testConnection() {
        const btn = byId('sim-editor-test');
        const result = byId('sim-editor-result');
        result.innerHTML = '<span class="opacity-70">Testing the connection</span>';
        showErrors(null);
        setBusy(btn, true);
        try {
            const data = await api('/api/sims/test', {
                method: 'POST',
                body: { type: editing.type.type, settings: readForm().settings },
            });
            result.innerHTML = `<span class="${data.ok ? 'text-success' : 'text-error'}">${escapeHtml(data.message)}</span>`;
        } catch (err) {
            result.innerHTML = '';
            if (err.data && err.data.fields) showErrors(err.data.fields);
            else result.innerHTML = `<span class="text-error">${escapeHtml(err.message)}</span>`;
        }
        setBusy(btn, false);
    }

    async function save(e) {
        e.preventDefault();
        const btn = byId('sim-editor-save');
        showErrors(null);
        setBusy(btn, true);
        const body = readForm();
        try {
            const saved = editing.sim
                ? await api(`/api/sims/${encodeURIComponent(editing.sim.id)}`, { method: 'PUT', body })
                : await api('/api/sims', { method: 'POST', body: { type: editing.type.type, ...body } });
            byId('sim-editor').close();
            toast(`${saved.name} saved`, 'success');
            refresh();
        } catch (err) {
            if (err.data && err.data.fields) showErrors(err.data.fields);
            else toast(err.message, 'error');
        }
        setBusy(btn, false);
    }

    async function openSimulatorEditor(id) {
        const dialog = byId('sim-editor');
        if (!dialog) return;
        let types;
        try {
            types = await loadTypes();
        } catch (err) {
            toast(`Could not load simulator types: ${err.message}`, 'error');
            return;
        }
        if (id) {
            if (!sims.some((s) => s.id === id)) await refresh();
            const sim = sims.find((s) => s.id === id);
            const type = sim && types.find((t) => t.type === sim.type);
            if (!type) {
                toast('That simulator no longer exists.', 'error');
                return;
            }
            showForm(type, sim);
        } else {
            editing = null;
            byId('sim-editor-title').textContent = 'Add simulator';
            byId('sim-editor-type-list').innerHTML = types.map((t) => `
                <button type="button" class="btn h-auto py-4 flex-col gap-1" data-type="${escapeHtml(t.type)}">
                    <i data-lucide="radio" class="icon-md"></i>
                    <span>${escapeHtml(t.display_name)}</span>
                </button>`).join('');
            if (typeof lucide !== 'undefined') lucide.createIcons({ nodes: [byId('sim-editor-type-list')] });
            showStep('types');
        }
        if (!dialog.open) dialog.showModal();
    }

    window.openSimulatorEditor = openSimulatorEditor;

    document.addEventListener('DOMContentLoaded', () => {
        const menu = byId('sims-menu');
        if (!menu) return;
        const btn = byId('sims-nav-btn');
        menu.addEventListener('toggle', () => btn.setAttribute('aria-expanded', String(menu.open)));
        document.addEventListener('click', (e) => {
            if (menu.open && !menu.contains(e.target)) menu.open = false;
        });
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape' && menu.open) {
                menu.open = false;
                btn.focus();
            }
        });
        byId('sims-list').addEventListener('click', onListClick);
        byId('sims-add-btn').addEventListener('click', () => {
            closeMenu();
            openSimulatorEditor();
        });

        byId('sim-editor-type-list').addEventListener('click', async (e) => {
            const btn = e.target.closest('[data-type]');
            if (!btn) return;
            const type = (await loadTypes()).find((t) => t.type === btn.dataset.type);
            if (type) showForm(type, null);
        });
        byId('sim-editor-form').addEventListener('submit', save);
        byId('sim-editor-test').addEventListener('click', testConnection);
        byId('sim-editor-cancel').addEventListener('click', () => byId('sim-editor').close());

        openSocket('/ws/sims', (data) => {
            if (data.type === 'sim_status') render(data.sims || []);
        });
    });
})();
