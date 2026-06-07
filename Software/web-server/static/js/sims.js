// sims.js - simulators drawer: live status over /ws/sims, connect/disconnect controls
(function () {
    let ws = null;

    function statusColor(status) {
        if (status === 'connected') return 'var(--color-success, #16a34a)';
        if (status === 'connecting' || status === 'error') return 'var(--color-warning, #d97706)';
        return 'var(--color-neutral, #9ca3af)';
    }

    function aggregate(sims) {
        if (sims.some((s) => s.status === 'connected')) return 'connected';
        if (sims.some((s) => s.status === 'connecting' || s.status === 'error')) return 'connecting';
        return 'off';
    }

    function render(sims) {
        const dot = document.getElementById('sims-status-dot');
        if (dot) dot.style.background = statusColor(aggregate(sims));

        const list = document.getElementById('sims-list');
        if (!list) return;
        if (!sims.length) {
            list.innerHTML =
                '<p class="opacity-60 text-sm">No simulators enabled. Enable one in Configuration.</p>';
            return;
        }
        list.innerHTML = sims
            .map((s) => {
                const connected = s.status === 'connected';
                const connecting = s.status === 'connecting';
                const action = connected ? 'disconnect' : 'connect';
                const label = connecting ? 'Connecting…' : connected ? 'Disconnect' : 'Connect';
                const disabled = connecting ? 'disabled' : '';
                return `
                <div class="border border-base-300 rounded-box p-3 mb-2">
                    <div class="flex items-center gap-2">
                        <span class="inline-block w-2 h-2 rounded-full" style="background:${statusColor(s.status)}"></span>
                        <span class="font-medium">${s.display_name || s.name}</span>
                        <span class="opacity-60 text-xs ml-auto">${s.status}</span>
                    </div>
                    <div class="opacity-70 text-xs mt-1">${s.target || ''} ${s.detail ? '· ' + s.detail : ''}</div>
                    <button class="btn btn-xs mt-2" data-sim="${s.name}" data-action="${action}" ${disabled}>${label}</button>
                </div>`;
            })
            .join('');

        list.querySelectorAll('button[data-sim]').forEach((btn) => {
            btn.addEventListener('click', async () => {
                btn.disabled = true;
                try {
                    const r = await fetch(`/api/sims/${btn.dataset.sim}/${btn.dataset.action}`, {
                        method: 'POST',
                    });
                    const data = await r.json();
                    if (data.sims) render(data.sims);
                } catch (e) {
                    console.error('sim action failed', e);
                } finally {
                    btn.disabled = false;
                }
            });
        });
    }

    async function loadInitial() {
        try {
            const r = await fetch('/api/sims');
            const data = await r.json();
            render(data.sims || []);
        } catch (e) {
            console.error('failed to load sims', e);
        }
    }

    function connectWs() {
        const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
        ws = new WebSocket(`${protocol}//${window.location.host}/ws/sims`);
        ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            if (data.type === 'sim_status') render(data.sims || []);
        };
        ws.onclose = () => setTimeout(connectWs, 3000);
        ws.onerror = () => {
            if (ws) ws.close();
        };
    }

    document.addEventListener('DOMContentLoaded', () => {
        const btn = document.getElementById('sims-nav-btn');
        const drawer = document.getElementById('sims-drawer');
        if (btn && drawer) {
            btn.addEventListener('click', () => drawer.classList.toggle('hidden'));
        }
        loadInitial();
        connectWs();
    });
})();
