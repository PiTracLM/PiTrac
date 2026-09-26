// config.js - Setup card, category settings with an advanced toggle, search, diff, import and export
/* global api, toast, confirmDialog, openSocket, onPiTracStatus, controlPiTrac, openSimulatorEditor, Option */
(function () {
    const ADVANCED_KEY = 'pitrac-config-advanced';
    const GOLFER_KEYS = [
        'gs_config.player.kGolferOrientation',
        'gs_config.player.kUsePracticeBalls',
        'gs_config.modes.kStartInPuttingMode',
    ];
    // Model dropdowns list { name: path } instead of { value: label }
    const MODEL_KEYS = ['gs_config.ball_identification.kModelPath', 'gs_config.spin_analysis.kSpinModelPath'];
    const NUMERIC = ['integer', 'number', 'float'];

    let meta = {};
    let categories = {};
    let config = {};
    let defaults = {};
    let user = {};
    let calibrated = new Set();
    let dependencies = new Set();
    const pending = new Map();
    const errors = new Map();
    // Tags of our own writes (key, config_reset, config_import) to the time they were sent, so their /ws echo
    // is not reported as a change from another device. Tags expire because a dropped socket never echoes.
    const selfEcho = new Map();
    const ECHO_MS = 5000;
    const view = { category: null, search: '' };
    let showAdvanced = false;
    let piTracRunning = false;
    let saving = false;
    let remoteTimer = null;

    const byId = (id) => document.getElementById(id);
    const getPath = (obj, key) => key.split('.').reduce((o, part) => o?.[part], obj);
    const saved = (key) => getPath(config, key);
    const defaultOf = (key) => getPath(defaults, key);
    const current = (key) => (pending.has(key) ? pending.get(key) : saved(key));
    const isCustom = (key) => getPath(user, key) !== undefined;
    const isAdvanced = (key) => meta[key]?.subcategory !== 'basic';
    const toBool = (v) => v === true || v === 1 || v === '1' || v === 'true';
    const plural = (n, word) => `${n} ${word}${n === 1 ? '' : 's'}`;

    function countLeaves(obj) {
        return Object.values(obj).reduce(
            (n, v) => n + (v && typeof v === 'object' && !Array.isArray(v) ? countLeaves(v) : 1), 0);
    }

    function displayName(key) {
        return meta[key]?.displayName
            || key.split('.').pop().replace(/^k/, '').replace(/([A-Z])/g, ' $1').trim();
    }

    function el(tag, className, text) {
        const node = document.createElement(tag);
        if (className) node.className = className;
        if (text !== undefined) node.textContent = text;
        return node;
    }

    function badge(text, tone) {
        return el('span', `badge badge-sm whitespace-nowrap ${tone}`, text);
    }

    function smallButton(label, onClick) {
        const btn = el('button', 'btn btn-ghost btn-xs', label);
        btn.type = 'button';
        btn.addEventListener('click', onClick);
        return btn;
    }

    function normalizeJson(v) {
        if (typeof v !== 'string') return JSON.stringify(v ?? null);
        try { return JSON.stringify(JSON.parse(v)); } catch { return v; }
    }

    function sameValue(key, a, b) {
        const type = meta[key]?.type;
        if (type === 'boolean') return toBool(a) === toBool(b);
        if (a === '' || a == null || b === '' || b == null) return (a ?? '') === (b ?? '');
        if (NUMERIC.includes(type)) return Number(a) === Number(b);
        if (type === 'array') return normalizeJson(a) === normalizeJson(b);
        return String(a) === String(b);
    }

    function modelName(path) {
        return String(path).split('/').filter((p) => p && p !== 'weights').pop() || String(path);
    }

    function optionsFor(key) {
        const entries = Object.entries(meta[key]?.options || {});
        return MODEL_KEYS.includes(key) ? entries.map(([name, path]) => [path, name]) : entries;
    }

    function formatValue(key, v) {
        if (v === undefined || v === null || v === '') return 'Not set';
        const type = meta[key]?.type;
        if (type === 'boolean') return toBool(v) ? 'On' : 'Off';
        if (type === 'select') {
            const match = optionsFor(key).find(([value]) => String(value) === String(v));
            return match ? match[1] : MODEL_KEYS.includes(key) ? modelName(v) : String(v);
        }
        return typeof v === 'object' ? JSON.stringify(v) : String(v);
    }

    function visible(key) {
        return Object.entries(meta[key]?.visibleWhen || {}).every(([k, v]) => String(current(k)) === String(v));
    }

    function clientError(key, value) {
        const m = meta[key] || {};
        if (NUMERIC.includes(m.type)) {
            const n = Number(value);
            if (value === '' || value === null || !Number.isFinite(n)) return 'Must be a number';
            if (m.min !== undefined && n < m.min) return `Must be at least ${m.min}`;
            if (m.max !== undefined && n > m.max) return `Must be at most ${m.max}`;
        }
        if (m.type === 'array' && typeof value === 'string') {
            try {
                if (!Array.isArray(JSON.parse(value))) return 'Must be a list like [1, 2, 3]';
            } catch {
                return 'Must be a list like [1, 2, 3]';
            }
        }
        return null;
    }

    // -- Controls and rows --

    function setControl(control, value) {
        if (control.type === 'checkbox') control.checked = toBool(value);
        else if (value !== null && typeof value === 'object') control.value = JSON.stringify(value);
        else control.value = value ?? '';
    }

    const controlValue = (control) => (control.type === 'checkbox' ? control.checked : control.value);

    function createControl(key, id) {
        const m = meta[key] || {};
        const value = current(key);
        let control;
        if (m.type === 'boolean') {
            control = el('input', 'toggle toggle-sm');
            control.type = 'checkbox';
        } else if (m.type === 'select') {
            control = el('select', 'select select-sm w-full');
            const options = optionsFor(key);
            if (value != null && value !== '' && !options.some(([v]) => String(v) === String(value))) {
                options.unshift([String(value), formatValue(key, value)]);
            }
            control.append(...options.map(([v, label]) => new Option(label, v)));
        } else if (NUMERIC.includes(m.type)) {
            control = el('input', 'input input-sm w-full');
            control.type = 'number';
            if (m.min !== undefined) control.min = m.min;
            if (m.max !== undefined) control.max = m.max;
            control.step = m.step ?? (m.type === 'integer' ? 1 : 'any');
        } else if (m.type === 'array') {
            control = el('textarea', 'textarea textarea-sm w-full font-mono');
            control.rows = 2;
        } else {
            control = el('input', 'input input-sm w-full');
            control.type = 'text';
        }
        control.id = id;
        control.dataset.key = key;
        control.classList.add('cfg-control');
        setControl(control, value);
        return control;
    }

    function numberHint(key) {
        const m = meta[key] || {};
        if (!NUMERIC.includes(m.type)) return '';
        const def = m.default ?? null;
        const range = m.min !== undefined && m.max !== undefined ? `Range ${m.min} to ${m.max}` : '';
        if (def === null) return range;
        return range ? `${range}, default ${def}` : `Default ${def}`;
    }

    function renderRow(key, prefix, restartNoted) {
        const m = meta[key] || {};
        const id = `${prefix}-${key}`;
        const row = el('div', 'config-row');
        row.dataset.key = key;
        row.hidden = !visible(key);
        if (restartNoted) row.dataset.restartNoted = '1';

        const info = el('div', 'min-w-0');
        const head = el('div', 'flex flex-wrap items-center gap-1.5');
        const label = el('label', 'text-sm font-medium', displayName(key));
        label.htmlFor = id;
        head.append(label, el('span', 'row-badges flex flex-wrap gap-1'));
        info.append(head);
        if (m.description) info.append(el('p', 'text-xs opacity-60 mt-0.5', m.description));
        const hint = numberHint(key);
        if (hint) info.append(el('p', 'text-xs opacity-50 mt-0.5', hint));

        const control = createControl(key, id);
        const error = el('p', 'row-error text-xs text-error');
        error.id = `${id}-error`;
        control.setAttribute('aria-describedby', error.id);
        // Saving the default over a calibrated value deletes the calibration entry
        if (calibrated.has(key)) {
            control.disabled = true;
            const note = el('p', 'text-xs opacity-60 mt-0.5', 'Set by calibration. Redo it on the ');
            const link = el('a', 'link', 'Calibration page');
            link.href = '/calibration';
            note.append(link, '.');
            info.append(note);
        }

        const side = el('div', 'config-row-control');
        side.append(control, error, el('div', 'row-actions flex flex-wrap justify-end gap-1'));
        row.append(info, side);
        refreshRow(row);
        return row;
    }

    function refreshRow(row) {
        const key = row.dataset.key;
        const isPending = pending.has(key);
        const isCalibrated = calibrated.has(key);
        const tags = [];
        if (isPending) tags.push(['Unsaved', 'badge-warning badge-soft']);
        if (isCalibrated) tags.push(['Calibrated', 'badge-info badge-soft']);
        else if (isCustom(key)) tags.push(['Custom', 'badge-primary badge-soft']);
        if (meta[key]?.requiresRestart && !row.dataset.restartNoted) tags.push(['Needs restart', 'badge-outline opacity-60']);
        if (isAdvanced(key)) tags.push(['Advanced', 'badge-outline opacity-60']);
        row.querySelector('.row-badges').replaceChildren(...tags.map(([text, tone]) => badge(text, tone)));

        const actions = [];
        if (isPending) actions.push(smallButton('Undo', () => setValue(key, saved(key))));
        if (isCustom(key) && !isCalibrated && !sameValue(key, current(key), defaultOf(key))) {
            actions.push(smallButton('Reset to default', () => setValue(key, defaultOf(key))));
        }
        row.querySelector('.row-actions').replaceChildren(...actions);

        const error = row.querySelector('.row-error');
        error.textContent = errors.get(key) || '';
        error.hidden = !errors.has(key);
        row.classList.toggle('is-pending', isPending);
    }

    function groupHeading(name, extra) {
        const wrap = el('div', 'config-group-heading flex items-end justify-between gap-2 border-b border-base-300 pb-1');
        wrap.append(el('h4', 'text-xs font-semibold uppercase tracking-wide text-base-content/60', name));
        if (extra) wrap.append(extra);
        return wrap;
    }

    function group(name, keys, prefix, { extra, restartNote } = {}) {
        const wrap = el('div', 'config-group');
        if (name) wrap.append(groupHeading(name, extra));
        const noted = restartNote && keys.every((key) => meta[key]?.requiresRestart);
        if (noted) wrap.append(el('p', 'text-xs opacity-60 mt-1', 'Changes here apply after PiTrac restarts.'));
        keys.forEach((key) => wrap.append(renderRow(key, prefix, noted)));
        return wrap;
    }

    // -- State changes --

    function setValue(key, value, source) {
        if (sameValue(key, value, saved(key))) pending.delete(key);
        else pending.set(key, value);
        const problem = clientError(key, value);
        if (problem) errors.set(key, problem);
        else errors.delete(key);

        document.querySelectorAll(`.cfg-control[data-key="${key}"]`).forEach((control) => {
            if (control !== source) setControl(control, value);
        });
        document.querySelectorAll(`.config-row[data-key="${key}"]`).forEach(refreshRow);
        if (dependencies.has(key)) {
            document.querySelectorAll('.config-row').forEach((row) => { row.hidden = !visible(row.dataset.key); });
            renderNav();
        }
        updateHeader();
    }

    function setAdvanced(on) {
        showAdvanced = on;
        byId('advanced-toggle').checked = on;
        try {
            localStorage.setItem(ADVANCED_KEY, on ? '1' : '0');
        } catch { /* storage blocked: the toggle resets on reload */ }
        renderNav();
        renderRows();
    }

    function selectCategory(category) {
        view.category = category;
        view.search = '';
        byId('search-input').value = '';
        renderNav();
        renderRows();
    }

    // -- Rendering --

    function categoryKeys(category) {
        const { basic = [], advanced = [] } = categories[category] || {};
        return { basic, advanced: showAdvanced ? advanced : [] };
    }

    function renderNav() {
        const items = Object.keys(categories).map((category) => {
            const { basic, advanced } = categoryKeys(category);
            return [category, [...basic, ...advanced].filter(visible).length];
        });
        byId('category-list').replaceChildren(...items.map(([category, count]) => {
            const btn = el('button', 'justify-between');
            btn.type = 'button';
            const active = !view.search && category === view.category;
            btn.classList.toggle('menu-active', active);
            if (active) btn.setAttribute('aria-current', 'true');
            btn.append(el('span', '', category), el('span', 'opacity-60 tabular-nums', String(count)));
            btn.addEventListener('click', () => selectCategory(category));
            const li = el('li');
            li.append(btn);
            return li;
        }));
        const select = byId('category-select');
        select.replaceChildren(...items.map(([category, count]) => new Option(`${category} (${count})`, category)));
        select.value = view.category;
    }

    function categoryView(category) {
        const frag = document.createDocumentFragment();
        frag.append(el('h3', 'text-base font-semibold', category));
        const { basic, advanced } = categoryKeys(category);
        if (!basic.length && !showAdvanced) {
            const empty = el('div', 'flex flex-col items-start gap-2 mt-3');
            const btn = el('button', 'btn btn-sm', 'Show advanced settings');
            btn.type = 'button';
            btn.addEventListener('click', () => setAdvanced(true));
            empty.append(el('p', 'text-sm opacity-70', 'Everything in this category is advanced.'), btn);
            frag.append(empty);
            return frag;
        }
        const groups = new Map();
        basic.forEach((key) => {
            const name = meta[key]?.basicSubcategory || '';
            groups.set(name, [...(groups.get(name) || []), key]);
        });
        [...groups].sort(([a], [b]) => (a ? 1 : 0) - (b ? 1 : 0))
            .forEach(([name, keys]) => frag.append(group(name === category ? '' : name, keys, 'cfg')));
        if (advanced.length) frag.append(group('Advanced', advanced, 'cfg'));
        return frag;
    }

    function searchResults() {
        const query = view.search.toLowerCase();
        const matches = (key) => [key, displayName(key), meta[key]?.description || '']
            .some((text) => text.toLowerCase().includes(query));
        const frag = document.createDocumentFragment();
        const summary = el('p', 'text-sm opacity-70');
        frag.append(summary);
        let total = 0;
        let anyAdvanced = false;
        Object.entries(categories).forEach(([category, { basic = [], advanced = [] }]) => {
            const keys = [...basic, ...advanced].filter((key) => visible(key) && matches(key));
            if (!keys.length) return;
            total += keys.length;
            anyAdvanced = anyAdvanced || keys.some(isAdvanced);
            const section = el('div', 'config-search-group');
            section.append(el('h3', 'text-base font-semibold', category));
            keys.forEach((key) => section.append(renderRow(key, 'cfg')));
            frag.append(section);
        });
        summary.textContent = total
            ? `${plural(total, 'result')}${anyAdvanced ? '. Includes advanced settings.' : ''}`
            : `No settings match "${view.search}".`;
        return frag;
    }

    function renderRows() {
        byId('settings-rows').replaceChildren(view.search ? searchResults() : categoryView(view.category));
    }

    function renderSetup() {
        const keys = Object.keys(meta).filter((key) => meta[key].setup && !meta[key].internal);
        const detect = el('button', 'btn btn-xs mb-1', 'Detect cameras');
        detect.type = 'button';
        detect.addEventListener('click', () => detectCameras(detect));
        byId('setup-groups').replaceChildren(
            group('Hardware', keys.filter((key) => !GOLFER_KEYS.includes(key)), 'setup', { extra: detect, restartNote: true }),
            group('Golfer', GOLFER_KEYS.filter((key) => keys.includes(key)), 'setup', { restartNote: true }),
        );
    }

    function updateHeader() {
        const n = pending.size;
        const save = byId('save-btn');
        save.disabled = !n || saving;
        save.textContent = saving ? 'Saving' : n ? `Save ${plural(n, 'change')}` : 'Save';
        byId('discard-btn').disabled = !n || saving;
        const custom = countLeaves(user);
        byId('custom-count').textContent = custom ? `${plural(custom, 'custom setting')}` : 'No custom settings';
    }

    function render() {
        renderSetup();
        renderNav();
        renderRows();
        updateHeader();
    }

    function showLoadError(err) {
        console.error('Could not load configuration:', err);
        const alert = el('div', 'alert alert-error alert-soft');
        const retry = el('button', 'btn btn-sm', 'Retry');
        retry.type = 'button';
        retry.addEventListener('click', load);
        alert.append(el('span', '', 'Could not load the configuration.'), retry);
        byId('setup-groups').replaceChildren(alert);
        byId('settings-rows').replaceChildren();
    }

    // -- Server data --

    async function refreshSaved() {
        const [c, u, cal] = await Promise.all([api('/api/config'), api('/api/config/user'), api('/api/config/calibrated')]);
        config = c.data || {};
        user = u.data || {};
        calibrated = new Set(cal || []);
        for (const [key, value] of pending) {
            if (sameValue(key, value, saved(key))) pending.delete(key);
        }
        render();
    }

    async function load() {
        try {
            const [m, cats, d] = await Promise.all([
                api('/api/config/metadata'), api('/api/config/categories'), api('/api/config/defaults'),
            ]);
            meta = m || {};
            categories = cats || {};
            defaults = d.data || {};
            dependencies = new Set(Object.values(meta).flatMap((s) => Object.keys(s.visibleWhen || {})));
            if (!categories[view.category]) view.category = Object.keys(categories)[0];
            await refreshSaved();
        } catch (err) {
            showLoadError(err);
            return;
        }
        if (location.hash === '#setup') byId('setup').scrollIntoView();
    }

    // -- Actions --

    async function save() {
        if (!pending.size || saving) return;
        saving = true;
        updateHeader();
        const failed = [];
        let restart = false;
        let count = 0;
        for (const [key, value] of [...pending]) {
            selfEcho.set(key, Date.now());
            try {
                const res = await api(`/api/config/${key}`, { method: 'PUT', body: { value } });
                if (sameValue(key, pending.get(key), value)) pending.delete(key);
                errors.delete(key);
                restart = restart || !!res?.requires_restart;
                count += 1;
            } catch (err) {
                selfEcho.delete(key);
                errors.set(key, err.message);
                failed.push(key);
            }
        }
        saving = false;
        try {
            await refreshSaved();
        } catch (err) {
            render();
            toast(`Saved, but the page could not reload the settings: ${err.message}`, 'warning');
        }
        if (failed.length === 1) {
            toast(`Could not save ${displayName(failed[0])}: ${errors.get(failed[0])}`, 'error');
        } else if (failed.length) {
            toast(`Could not save ${plural(failed.length, 'setting')}: ${failed.map(displayName).join(', ')}`, 'error');
        }
        if (count) toast(`Saved ${plural(count, 'change')}`, 'success');
        if (restart && piTracRunning) {
            toast('Restart PiTrac to apply', 'warning', {
                sticky: true, actionLabel: 'Restart now', onAction: () => controlPiTrac('restart'),
            });
        }
    }

    async function discard() {
        const ok = await confirmDialog({
            title: 'Discard changes?',
            body: `Your ${plural(pending.size, 'unsaved change')} will be lost.`,
            confirmLabel: 'Discard',
            danger: true,
        });
        if (!ok) return;
        pending.clear();
        errors.clear();
        render();
    }

    async function putDefault(key) {
        selfEcho.set(key, Date.now());
        try {
            await api(`/api/config/${key}`, { method: 'PUT', body: { value: defaultOf(key) } });
        } catch (err) {
            selfEcho.delete(key);
            throw err;
        }
        pending.delete(key);
        errors.delete(key);
    }

    async function showDiff() {
        let diff;
        try {
            diff = (await api('/api/config/diff')).data || {};
        } catch (err) {
            toast(`Could not load the diff: ${err.message}`, 'error');
            return;
        }
        for (const [key, value] of pending) diff[key] = { user: value, default: defaultOf(key), source: 'unsaved' };

        const entries = Object.entries(diff);
        byId('diff-summary').textContent = entries.length
            ? `${plural(entries.length, 'setting')} differ from the defaults.`
            : 'Everything matches the defaults.';
        const list = el('ul', 'flex flex-col');
        entries.forEach(([key, { user: mine, default: def, source }]) => {
            const item = el('li', 'flex flex-wrap items-start gap-x-4 gap-y-1 py-3 border-b border-base-300 last:border-0');
            const name = el('div', 'min-w-0 flex-1 basis-48');
            const title = el('div', 'flex flex-wrap items-center gap-1.5');
            title.append(el('span', 'font-medium', displayName(key)));
            if (source === 'calibration') title.append(badge('Calibrated', 'badge-info badge-soft'));
            if (source === 'unsaved') title.append(badge('Unsaved', 'badge-warning badge-soft'));
            name.append(title, el('div', 'text-xs font-mono opacity-50 break-all', key));

            const values = el('div', 'text-sm min-w-0 basis-full sm:basis-auto sm:max-w-[45%] break-all');
            const line = (label, value) => {
                const row = el('div');
                row.append(el('span', 'opacity-60', `${label} `), el('code', '', formatValue(key, value)));
                return row;
            };
            values.append(line('Default', def), line('Yours', mine));

            item.append(name, values);
            if (source === 'user') {
                item.append(smallButton('Reset', () => resetFromDiff(key)));
            } else if (source === 'unsaved') {
                item.append(smallButton('Undo', () => {
                    setValue(key, saved(key));
                    showDiff();
                }));
            }
            list.append(item);
        });
        byId('diff-body').replaceChildren(list);
        const dialog = byId('diff-dialog');
        if (!dialog.open) dialog.showModal();
    }

    async function resetFromDiff(key) {
        const ok = await confirmDialog({
            title: `Reset ${displayName(key)}?`,
            body: `It goes back to ${formatValue(key, defaultOf(key))}.`,
            confirmLabel: 'Reset',
        });
        if (!ok) return;
        try {
            await putDefault(key);
            await refreshSaved();
            toast(`${displayName(key)} is back to its default`, 'success');
        } catch (err) {
            toast(`Could not reset ${displayName(key)}: ${err.message}`, 'error');
        }
        showDiff();
    }

    function exportConfig() {
        const link = el('a');
        link.href = '/api/config/export';
        link.download = `pitrac-config-${new Date().toLocaleDateString('en-CA')}.json`;
        link.click();
    }

    async function importConfig(file) {
        let data;
        try {
            data = JSON.parse(await file.text());
        } catch {
            toast('That file is not a PiTrac settings export.', 'error');
            return;
        }
        const ok = await confirmDialog({
            title: `Import ${file.name}?`,
            body: data && data.calibration_data
                ? 'Your custom settings and calibration are replaced by the ones in this file.'
                : 'Your custom settings are replaced by the ones in this file.',
            confirmLabel: 'Import',
        });
        if (!ok) return;
        selfEcho.set('config_import', Date.now());
        try {
            await api('/api/config/import', { method: 'POST', body: data });
        } catch (err) {
            selfEcho.delete('config_import');
            toast(`Import failed: ${err.message}`, 'error');
            return;
        }
        await refreshSaved();
        toast('Settings imported', 'success');
    }

    async function resetAll() {
        const ok = await confirmDialog({
            title: 'Reset all settings?',
            body: 'Your custom settings go back to defaults. Calibration is kept.',
            confirmLabel: 'Reset all',
            danger: true,
        });
        if (!ok) return;
        selfEcho.set('config_reset', Date.now());
        try {
            await api('/api/config/reset', { method: 'POST' });
        } catch (err) {
            selfEcho.delete('config_reset');
            toast(`Could not reset: ${err.message}`, 'error');
            return;
        }
        pending.clear();
        errors.clear();
        await refreshSaved();
        toast('Settings are back to defaults. Calibration was kept.', 'success');
    }

    async function detectCameras(btn) {
        btn.disabled = true;
        try {
            const result = await api('/api/cameras/detect');
            const warnings = result.warnings || [];
            if (!result.success) {
                toast([result.message || 'No cameras found', ...warnings].join('. '), 'error');
                return;
            }
            setValue('cameras.slot1.type', String(result.configuration.slot1.type));
            if (result.cameras.length > 1) setValue('cameras.slot2.type', String(result.configuration.slot2.type));
            toast(`${result.message}. Save to keep it.`, 'success');
            warnings.forEach((w) => toast(w, 'warning'));
        } catch (err) {
            toast(`Camera detection failed: ${err.message}`, 'error');
        } finally {
            btn.disabled = false;
        }
    }

    async function manageSims() {
        let sims = null;
        try {
            sims = await api('/api/sims');
        } catch { /* the menu shows its own load error */ }
        if (Array.isArray(sims) && !sims.length) {
            openSimulatorEditor();
            return;
        }
        window.scrollTo({ top: 0, behavior: 'smooth' });
        byId('sims-menu').open = true;
        byId('sims-nav-btn').focus({ preventScroll: true });
    }

    function onSocket(data) {
        if (!data || typeof data.type !== 'string' || !data.type.startsWith('config_')) return;
        const tag = data.type === 'config_update' ? data.key : data.type;
        const sentAt = selfEcho.get(tag);
        selfEcho.delete(tag);
        if (sentAt !== undefined && Date.now() - sentAt < ECHO_MS) return;
        clearTimeout(remoteTimer);
        remoteTimer = setTimeout(async () => {
            try {
                await refreshSaved();
            } catch (err) {
                console.error('Could not reload configuration:', err);
            }
            toast('Settings changed on another device', 'info');
        }, 300);
    }

    // -- Wiring --

    document.addEventListener('DOMContentLoaded', () => {
        try {
            showAdvanced = localStorage.getItem(ADVANCED_KEY) === '1';
        } catch { /* storage blocked: basic settings only */ }
        byId('advanced-toggle').checked = showAdvanced;
        byId('advanced-toggle').addEventListener('change', (e) => setAdvanced(e.target.checked));
        byId('category-select').addEventListener('change', (e) => selectCategory(e.target.value));
        byId('search-input').addEventListener('input', (e) => {
            view.search = e.target.value.trim();
            renderNav();
            renderRows();
        });
        byId('save-btn').addEventListener('click', save);
        byId('discard-btn').addEventListener('click', discard);
        byId('manage-sims-btn').addEventListener('click', manageSims);

        const more = byId('config-more');
        const moreActions = { diff: showDiff, export: exportConfig, import: () => byId('import-file').click(), 'reset-all': resetAll };
        more.addEventListener('click', (e) => {
            const item = e.target.closest('[data-action]');
            if (!item) return;
            more.open = false;
            moreActions[item.dataset.action]();
        });
        document.addEventListener('click', (e) => {
            if (more.open && !more.contains(e.target)) more.open = false;
        });
        document.addEventListener('keydown', (e) => {
            if (e.key !== 'Escape' || !more.open) return;
            more.open = false;
            more.querySelector('summary').focus();
        });
        byId('import-file').addEventListener('change', (e) => {
            const file = e.target.files[0];
            e.target.value = '';
            if (file) importConfig(file);
        });

        const onEdit = (e) => {
            const control = e.target.closest?.('.cfg-control');
            if (control) setValue(control.dataset.key, controlValue(control), control);
        };
        document.addEventListener('input', onEdit);
        document.addEventListener('change', onEdit);

        onPiTracStatus((status) => { piTracRunning = status.is_running; });
        openSocket('/ws', onSocket, { onOpen: () => selfEcho.clear() });
        load();
    });

    window.addEventListener('beforeunload', (e) => {
        if (pending.size) e.preventDefault();
    });
})();
