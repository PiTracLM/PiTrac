// sims.js - simulators drawer: live status over /ws/sims, connect/disconnect controls
/* global api, toast, escapeHtml, openSocket */
(function () {
    const STATUS = {
        connected: { label: 'Connected', dot: 'bg-success' },
        connecting: { label: 'Connecting', dot: 'bg-warning' },
        error: { label: "Can't connect", dot: 'bg-error' },
        off: { label: 'Not connected', dot: 'bg-base-content/30' },
    };
    const OPEN_KEY = 'pitrac-sims-open';

    function statusOf(name) {
        return STATUS[name] || STATUS.off;
    }

    function aggregate(sims) {
        return ['error', 'connecting', 'connected'].find((st) => sims.some((s) => s.status === st)) || 'off';
    }

    function rowHtml(s) {
        const st = statusOf(s.status);
        const action = s.status === 'connected' ? 'disconnect' : 'connect';
        const label = s.status === 'connecting' ? 'Connecting' : s.status === 'connected' ? 'Disconnect' : 'Connect';
        const button = s.target
            ? `<button class="btn btn-xs ml-auto" data-sim="${escapeHtml(s.name)}" data-action="${action}" ${s.status === 'connecting' ? 'disabled' : ''}>${label}</button>`
            : '';
        const detail = s.target ? s.detail : s.detail || 'No host set in Configuration.';
        const detailClass = s.status === 'error' || !s.target ? 'text-error' : 'opacity-70';
        return `
            <div class="border border-base-300 rounded-box p-3 mb-2">
                <div class="flex flex-wrap items-center gap-2">
                    <span class="inline-block w-2 h-2 rounded-full ${st.dot}"></span>
                    <span class="font-medium">${escapeHtml(s.display_name || s.name)}</span>
                    <span class="text-sm opacity-80">${st.label}</span>
                    ${button}
                </div>
                ${s.target ? `<div class="font-mono text-xs opacity-70 mt-1">${escapeHtml(s.target)}</div>` : ''}
                ${detail && detail !== s.target ? `<div class="text-xs mt-1 break-words ${detailClass}">${escapeHtml(detail)}</div>` : ''}
            </div>`;
    }

    function render(data) {
        if (data.type !== 'sim_status') return;
        const sims = data.sims || [];
        document.dispatchEvent(new CustomEvent('pitrac:sims', { detail: sims }));

        const dot = document.getElementById('sims-status-dot');
        if (dot) dot.className = `w-2 h-2 rounded-full ${statusOf(aggregate(sims)).dot}`;

        const list = document.getElementById('sims-list');
        if (!list) return;
        list.innerHTML = sims.length
            ? sims.map(rowHtml).join('')
            : '<p class="text-sm opacity-70">No simulator is enabled yet. <a class="link link-primary" href="/config#setup">Set one up in Configuration</a></p>';
    }

    async function onListClick(e) {
        const btn = e.target.closest('button[data-sim]');
        if (!btn) return;
        btn.disabled = true;
        try {
            const data = await api(`/api/sims/${btn.dataset.sim}/${btn.dataset.action}`, { method: 'POST' });
            render({ type: 'sim_status', sims: data.sims });
        } catch (err) {
            toast(err.message, 'error');
            btn.disabled = false;
        }
    }

    function setOpen(drawer, btn, open) {
        drawer.classList.toggle('hidden', !open);
        btn.setAttribute('aria-expanded', String(open));
        try { localStorage.setItem(OPEN_KEY, open ? '1' : ''); } catch { /* storage unavailable */ }
    }

    document.addEventListener('DOMContentLoaded', () => {
        const btn = document.getElementById('sims-nav-btn');
        const drawer = document.getElementById('sims-drawer');
        if (btn && drawer) {
            let open = false;
            try { open = localStorage.getItem(OPEN_KEY) === '1'; } catch { /* storage unavailable */ }
            setOpen(drawer, btn, open);
            btn.addEventListener('click', () => setOpen(drawer, btn, drawer.classList.contains('hidden')));
        }
        document.getElementById('sims-list')?.addEventListener('click', onListClick);
        openSocket('/ws/sims', render);
    });
})();
