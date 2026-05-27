/**
 * PiTrac /pico page controller.
 *
 * Polls /api/pico/status at 1 Hz, drives the live RMS chart via
 * EventSource, and routes config saves + flash upload through the
 * /api/pico/* endpoints exposed by server.py.
 */

class PicoController {
    constructor() {
        this.statusInterval = null;
        this.rmsSource = null;
        this.rmsSamples = [];
        this.rmsCapacity = 240;
        this.canvas = null;
        this.ctx = null;
        this.lastStatus = null;

        this.init();
        this.setupPageCleanup();
    }

    init() {
        this.canvas = document.getElementById('pico-rms-canvas');
        this.ctx = this.canvas ? this.canvas.getContext('2d') : null;
        this.resizeCanvas();
        window.addEventListener('resize', () => this.resizeCanvas());

        this.setupEventListeners();
        this.refreshStatus();
        this.startStatusPolling();
    }

    setupPageCleanup() {
        const cleanup = () => this.cleanup();
        window.addEventListener('beforeunload', cleanup);
        window.addEventListener('pagehide', cleanup);
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) this.stopRmsStream();
        });
    }

    cleanup() {
        if (this.statusInterval) {
            clearInterval(this.statusInterval);
            this.statusInterval = null;
        }
        this.stopRmsStream();
    }

    setupEventListeners() {
        document.getElementById('pico-refresh-btn').addEventListener('click', () => this.refreshStatus());
        document.getElementById('pico-selftest-btn').addEventListener('click', () => this.runSelftest());

        document.getElementById('pico-armed-toggle').addEventListener('change', (e) => this.setArmed(e.target.checked));
        document.getElementById('pico-threshold-save').addEventListener('click', () => this.saveThreshold());
        document.getElementById('pico-min-inter-shot-save').addEventListener('click', () => this.saveMinInterShot());

        document.getElementById('pico-rms-start').addEventListener('click', () => this.startRmsStream());
        document.getElementById('pico-rms-stop').addEventListener('click', () => this.stopRmsStream());

        document.getElementById('pico-uf2-file').addEventListener('change', (e) => {
            document.getElementById('pico-flash-btn').disabled = e.target.files.length === 0;
        });
        document.getElementById('pico-flash-btn').addEventListener('click', () => this.flashFirmware());
    }

    startStatusPolling() {
        this.statusInterval = setInterval(() => this.refreshStatus(), 1000);
    }

    async refreshStatus() {
        try {
            const resp = await fetch('/api/pico/status');
            if (resp.status === 503) {
                const data = await resp.json();
                this.renderDisconnected(data);
                return;
            }
            if (!resp.ok) throw new Error(`status ${resp.status}`);
            const data = await resp.json();
            this.renderStatus(data);
        } catch (err) {
            this.renderDisconnected({ error: err.message });
        }
    }

    renderStatus(data) {
        this.lastStatus = data;

        const badge = document.getElementById('pico-conn-state');
        badge.textContent = 'Connected';
        badge.className = 'badge badge-success';

        document.getElementById('pico-fw').textContent = data.fw || data.fw_version || '--';
        document.getElementById('pico-device').textContent = data.device || '/dev/ttyACM0';
        document.getElementById('pico-vsys').textContent = data.vsys_mv ?? '--';
        document.getElementById('pico-vbus').textContent = (data.vbus === 1 || data.vbus === true) ? 'present' : 'absent';
        document.getElementById('pico-mic-rms').textContent = data.last_rms ?? data.mic_rms ?? '--';
        document.getElementById('pico-event-count').textContent = data.event_count ?? '--';

        const armed = data.armed === 1 || data.armed === true;
        const toggle = document.getElementById('pico-armed-toggle');
        if (document.activeElement !== toggle) toggle.checked = armed;
        document.getElementById('pico-armed-state').textContent = armed ? 'armed' : 'disarmed';

        const thresholdInput = document.getElementById('pico-threshold');
        if (document.activeElement !== thresholdInput && typeof data.threshold === 'number') {
            thresholdInput.value = data.threshold;
        }

        const minInput = document.getElementById('pico-min-inter-shot');
        if (document.activeElement !== minInput && typeof data.min_inter_shot_ms === 'number') {
            minInput.value = data.min_inter_shot_ms;
        }
    }

    renderDisconnected(_data) {
        const badge = document.getElementById('pico-conn-state');
        badge.textContent = 'Disconnected';
        badge.className = 'badge badge-error';
        document.getElementById('pico-fw').textContent = '--';
        if (this.rmsSource) this.stopRmsStream();
    }

    async runSelftest() {
        const out = document.getElementById('pico-selftest-output');
        out.textContent = 'Running SELFTEST...';
        try {
            const resp = await fetch('/api/pico/selftest', { method: 'POST' });
            const data = await resp.json();
            if (!resp.ok) {
                out.textContent = `Error: ${data.error || 'selftest failed'}`;
                return;
            }
            out.textContent = data.raw || JSON.stringify(data, null, 2);
        } catch (err) {
            out.textContent = `Error: ${err.message}`;
        }
    }

    async setArmed(value) {
        await this.postConfig({ armed: value });
    }

    async saveThreshold() {
        const value = Number(document.getElementById('pico-threshold').value);
        if (!Number.isInteger(value) || value < 0) {
            this.flashMessage('Threshold must be a non-negative integer', 'error');
            return;
        }
        await this.postConfig({ threshold: value });
    }

    async saveMinInterShot() {
        const value = Number(document.getElementById('pico-min-inter-shot').value);
        if (!Number.isInteger(value) || value < 0) {
            this.flashMessage('min_inter_shot must be a non-negative integer', 'error');
            return;
        }
        await this.postConfig({ min_inter_shot_ms: value });
    }

    async postConfig(payload) {
        try {
            const resp = await fetch('/api/pico/config', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload)
            });
            const data = await resp.json();
            if (!resp.ok) {
                const detail = data.errors ? Object.values(data.errors).join('; ') : (data.error || 'config rejected');
                this.flashMessage(detail, 'error');
                return;
            }
            this.flashMessage('Saved', 'success');
            // Status poll will pick up the new values within ~1 s.
        } catch (err) {
            this.flashMessage(err.message, 'error');
        }
    }

    startRmsStream() {
        if (this.rmsSource) return;
        const hz = document.getElementById('pico-rms-hz').value || '20';
        this.rmsSamples = [];
        this.rmsSource = new EventSource(`/api/pico/rms-stream?hz=${hz}`);
        this.rmsSource.onmessage = (event) => {
            try {
                const payload = JSON.parse(event.data);
                this.pushRmsSample(payload);
            } catch {
                // Ignore malformed frames; the firmware never emits these but a proxy might.
            }
        };
        this.rmsSource.onerror = () => {
            // Browser auto-reconnects EventSource. We just surface the state.
            document.getElementById('pico-rms-latest').textContent = '(stream error, retrying)';
        };

        document.getElementById('pico-rms-start').disabled = true;
        document.getElementById('pico-rms-stop').disabled = false;
    }

    stopRmsStream() {
        if (this.rmsSource) {
            this.rmsSource.close();
            this.rmsSource = null;
        }
        document.getElementById('pico-rms-start').disabled = false;
        document.getElementById('pico-rms-stop').disabled = true;
    }

    pushRmsSample(sample) {
        this.rmsSamples.push(sample);
        if (this.rmsSamples.length > this.rmsCapacity) {
            this.rmsSamples.shift();
        }
        document.getElementById('pico-rms-latest').textContent = sample.value;
        this.drawRms();
    }

    resizeCanvas() {
        if (!this.canvas) return;
        const ratio = window.devicePixelRatio || 1;
        const cssW = this.canvas.clientWidth || 600;
        const cssH = this.canvas.clientHeight || 160;
        this.canvas.width = Math.floor(cssW * ratio);
        this.canvas.height = Math.floor(cssH * ratio);
        if (this.ctx) this.ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
        this.drawRms();
    }

    drawRms() {
        if (!this.ctx || !this.canvas) return;
        const ratio = window.devicePixelRatio || 1;
        const w = this.canvas.width / ratio;
        const h = this.canvas.height / ratio;
        this.ctx.clearRect(0, 0, w, h);

        if (this.rmsSamples.length < 2) return;

        const values = this.rmsSamples.map((s) => s.value);
        const peak = Math.max(1, ...values);

        this.ctx.strokeStyle = 'rgba(100, 116, 139, 0.4)';
        this.ctx.lineWidth = 1;
        const thresholdValue = Number(document.getElementById('pico-threshold').value);
        if (thresholdValue > 0) {
            const y = h - (Math.min(thresholdValue, peak) / peak) * (h - 8) - 4;
            this.ctx.beginPath();
            this.ctx.moveTo(0, y);
            this.ctx.lineTo(w, y);
            this.ctx.stroke();
        }

        this.ctx.strokeStyle = '#3b82f6';
        this.ctx.lineWidth = 2;
        this.ctx.beginPath();
        values.forEach((v, i) => {
            const x = (i / (this.rmsCapacity - 1)) * w;
            const y = h - (v / peak) * (h - 8) - 4;
            if (i === 0) this.ctx.moveTo(x, y); else this.ctx.lineTo(x, y);
        });
        this.ctx.stroke();
    }

    async flashFirmware() {
        const fileInput = document.getElementById('pico-uf2-file');
        const log = document.getElementById('pico-flash-log');
        const btn = document.getElementById('pico-flash-btn');

        if (!fileInput.files.length) return;
        const form = new FormData();
        form.append('uf2', fileInput.files[0]);

        log.textContent = `Uploading ${fileInput.files[0].name}...\n`;
        btn.disabled = true;

        try {
            const resp = await fetch('/api/pico/flash', { method: 'POST', body: form });
            if (!resp.body) {
                log.textContent += '\nError: server did not stream a response';
                return;
            }
            const reader = resp.body.getReader();
            const decoder = new TextDecoder();
            let done = false;
            while (!done) {
                const chunk = await reader.read();
                done = chunk.done;
                if (chunk.value) {
                    log.textContent += decoder.decode(chunk.value, { stream: true });
                    log.scrollTop = log.scrollHeight;
                }
            }
        } catch (err) {
            log.textContent += `\nError: ${err.message}`;
        } finally {
            btn.disabled = false;
        }
    }

    flashMessage(text, type) {
        const badge = document.getElementById('pico-conn-state');
        // Borrow the status badge for transient feedback.
        const prev = { text: badge.textContent, cls: badge.className };
        badge.textContent = text;
        badge.className = `badge ${type === 'error' ? 'badge-error' : 'badge-success'}`;
        setTimeout(() => {
            badge.textContent = prev.text;
            badge.className = prev.cls;
        }, 1500);
    }
}

window.PicoController = PicoController;
const pico = new PicoController();
window.pico = pico;
