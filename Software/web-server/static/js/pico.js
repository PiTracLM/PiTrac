const STREAM_STALE_MS = 3000;

function formatThousands(value) {
    const v = Number(value) || 0;
    if (v >= 1_000_000) return (v / 1_000_000).toFixed(2) + 'M';
    if (v >= 1_000) return (v / 1_000).toFixed(1) + 'k';
    return String(Math.round(v));
}

class PicoController {
    constructor() {
        this.statusInterval = null;
        this.rmsSource = null;
        this.rmsSamples = [];
        this.rmsCapacity = 240;
        this.rmsPeak = 0;
        this.rmsLastEventAt = 0;
        this.rmsStaleTimer = null;
        this.rmsPaused = false;
        this.canvas = null;
        this.ctx = null;
        this.lastStatus = null;
        this.flashInProgress = false;
        this.userTouchedThreshold = false;

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
        this.startRmsStream();
    }

    setupPageCleanup() {
        const cleanup = () => this.cleanup();
        window.addEventListener('beforeunload', cleanup);
        window.addEventListener('pagehide', cleanup);
        document.addEventListener('visibilitychange', () => {
            if (document.hidden) {
                this.stopRmsStream();
            } else if (!this.rmsPaused && !this.flashInProgress) {
                this.startRmsStream();
            }
        });
    }

    cleanup() {
        if (this.statusInterval) {
            clearInterval(this.statusInterval);
            this.statusInterval = null;
        }
        if (this.rmsStaleTimer) {
            clearInterval(this.rmsStaleTimer);
            this.rmsStaleTimer = null;
        }
        this.stopRmsStream();
    }

    setupEventListeners() {
        document.getElementById('pico-refresh-btn').addEventListener('click', () => this.refreshStatus());
        document.getElementById('pico-selftest-btn').addEventListener('click', () => this.runSelftest());

        document.getElementById('pico-armed-toggle').addEventListener('change', (e) => this.setArmed(e.target.checked));
        document.getElementById('pico-min-inter-shot-save').addEventListener('click', () => this.saveMinInterShot());

        const thresholdSlider = document.getElementById('pico-threshold');
        thresholdSlider.addEventListener('input', () => {
            this.userTouchedThreshold = true;
            document.getElementById('pico-threshold-value').textContent = thresholdSlider.value;
            this.drawRms();
        });
        thresholdSlider.addEventListener('change', () => this.saveThreshold());

        document.getElementById('pico-rms-toggle').addEventListener('click', () => this.toggleRmsStream());
        document.getElementById('pico-rms-hz').addEventListener('change', () => this.restartRmsStream());

        document.getElementById('pico-uf2-file').addEventListener('change', (e) => {
            document.getElementById('pico-flash-btn').disabled = e.target.files.length === 0;
        });
        document.getElementById('pico-flash-btn').addEventListener('click', () => this.flashFromUpload());
        document.getElementById('pico-flash-bundled-btn').addEventListener('click', () => this.flashBundled());
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
        document.getElementById('pico-vbus').textContent = (data.vbus === 1 || data.vbus === true) ? 'present' : 'absent';
        document.getElementById('pico-event-count').textContent = data.event_count ?? '--';

        const armed = data.armed === 1 || data.armed === true;
        const toggle = document.getElementById('pico-armed-toggle');
        if (document.activeElement !== toggle) toggle.checked = armed;
        document.getElementById('pico-armed-state').textContent = armed ? 'armed' : 'disarmed';

        const thresholdInput = document.getElementById('pico-threshold');
        if (!this.userTouchedThreshold
            && document.activeElement !== thresholdInput
            && typeof data.threshold === 'number') {
            this.ensureSliderRange(data.threshold);
            thresholdInput.value = data.threshold;
            document.getElementById('pico-threshold-value').textContent = data.threshold;
            this.drawRms();
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
        this.setRmsHint('Pico disconnected. Reconnect USB and click Refresh.');
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
        } catch (err) {
            this.flashMessage(err.message, 'error');
        }
    }

    startRmsStream() {
        if (this.rmsSource) return;
        const hz = document.getElementById('pico-rms-hz').value || '20';
        this.rmsSamples = [];
        this.rmsPeak = 0;
        this.rmsLastEventAt = Date.now();
        this.rmsPaused = false;
        this.setRmsHint('Connecting to stream...');

        this.rmsSource = new EventSource(`/api/pico/rms-stream?hz=${hz}`);
        this.rmsSource.onmessage = (event) => {
            try {
                const payload = JSON.parse(event.data);
                this.pushRmsSample(payload);
            } catch {
                // ignore malformed frames
            }
        };
        this.rmsSource.onerror = () => {
            this.setRmsHint('Stream interrupted, browser will retry...');
        };

        if (this.rmsStaleTimer) clearInterval(this.rmsStaleTimer);
        this.rmsStaleTimer = setInterval(() => this.checkRmsStale(), 1000);
        this.updateRmsToggleButton();
    }

    stopRmsStream() {
        if (this.rmsSource) {
            this.rmsSource.close();
            this.rmsSource = null;
        }
        if (this.rmsStaleTimer) {
            clearInterval(this.rmsStaleTimer);
            this.rmsStaleTimer = null;
        }
        this.updateRmsToggleButton();
    }

    restartRmsStream() {
        this.stopRmsStream();
        if (!this.rmsPaused && !this.flashInProgress) this.startRmsStream();
    }

    toggleRmsStream() {
        this.rmsPaused = !this.rmsPaused;
        if (this.rmsPaused) {
            this.stopRmsStream();
            this.setRmsHint('Paused');
        } else {
            this.startRmsStream();
        }
    }

    updateRmsToggleButton() {
        const btn = document.getElementById('pico-rms-toggle');
        if (!btn) return;
        const icon = btn.querySelector('i');
        const label = btn.querySelector('span');
        if (this.rmsSource) {
            if (icon) icon.setAttribute('data-lucide', 'pause');
            if (label) label.textContent = 'Pause';
        } else {
            if (icon) icon.setAttribute('data-lucide', 'play');
            if (label) label.textContent = 'Resume';
        }
        if (window.lucide) window.lucide.createIcons();
    }

    checkRmsStale() {
        if (!this.rmsSource) return;
        if (this.rmsLastEventAt === 0) return;
        const age = Date.now() - this.rmsLastEventAt;
        if (age < STREAM_STALE_MS) return;
        const fw = this.lastStatus && this.lastStatus.fw;
        if (fw && fw !== '0.5.0' && !fw.startsWith('0.5') && !fw.startsWith('1.')) {
            this.setRmsHint(`No events. Pico fw=${fw} predates CFG STREAM_RMS - flash 0.5.0 via the Firmware card below.`);
        } else {
            this.setRmsHint('No events. Check Pico mic wiring or USB-CDC link.');
        }
    }

    setRmsHint(text) {
        const el = document.getElementById('pico-rms-hint');
        if (el) el.textContent = text;
    }

    pushRmsSample(sample) {
        this.rmsLastEventAt = Date.now();
        this.setRmsHint('Streaming');
        this.rmsSamples.push(sample);
        if (this.rmsSamples.length > this.rmsCapacity) {
            this.rmsSamples.shift();
        }
        if (sample.value > this.rmsPeak) this.rmsPeak = sample.value;
        document.getElementById('pico-rms-latest').textContent = sample.value;
        document.getElementById('pico-rms-peak').textContent = this.rmsPeak;
        this.ensureSliderRange(this.rmsPeak);
        this.drawRms();
    }

    ensureSliderRange(observedValue) {
        const slider = document.getElementById('pico-threshold');
        const target = Math.max(100000, Math.ceil(observedValue * 2 / 10000) * 10000);
        const currentMax = Number(slider.max) || 100000;
        if (target > currentMax) {
            slider.max = String(target);
            slider.step = String(Math.max(1000, Math.floor(target / 200)));
            document.getElementById('pico-threshold-max').textContent = formatThousands(target);
        }
    }

    resizeCanvas() {
        if (!this.canvas) return;
        const ratio = window.devicePixelRatio || 1;
        const cssW = this.canvas.clientWidth || 600;
        const cssH = this.canvas.clientHeight || 120;
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

        const topPad = 6;
        const bottomPad = 18;
        const usableH = h - topPad - bottomPad;

        const thresholdValue = Number(document.getElementById('pico-threshold').value) || 0;
        const sampleMax = this.rmsSamples.length ? Math.max(...this.rmsSamples.map((s) => s.value)) : 0;
        // Y scale tracks samples, not threshold, so the slider visibly moves the line.
        const peak = Math.max(1, sampleMax * 1.1, this.rmsPeak * 0.4);

        if (thresholdValue > 0) {
            const clamped = Math.min(thresholdValue, peak);
            const y = topPad + (1 - clamped / peak) * usableH;
            this.ctx.strokeStyle = '#f97316';
            this.ctx.lineWidth = thresholdValue > peak ? 2 : 1;
            this.ctx.setLineDash([4, 4]);
            this.ctx.beginPath();
            this.ctx.moveTo(0, y);
            this.ctx.lineTo(w, y);
            this.ctx.stroke();
            this.ctx.setLineDash([]);

            this.ctx.fillStyle = '#f97316';
            this.ctx.font = '10px ui-monospace, monospace';
            this.ctx.textAlign = 'right';
            const label = thresholdValue > peak ? `> ${formatThousands(peak)}` : formatThousands(thresholdValue);
            this.ctx.fillText(label, w - 4, Math.max(y - 3, 10));
        }

        if (this.rmsSamples.length >= 2) {
            const values = this.rmsSamples.map((s) => s.value);
            this.ctx.strokeStyle = '#3b82f6';
            this.ctx.lineWidth = 2;
            this.ctx.beginPath();
            values.forEach((v, i) => {
                const x = (i / (this.rmsCapacity - 1)) * w;
                const y = topPad + (1 - Math.min(v, peak) / peak) * usableH;
                if (i === 0) this.ctx.moveTo(x, y); else this.ctx.lineTo(x, y);
            });
            this.ctx.stroke();
        }

        const hz = Number(document.getElementById('pico-rms-hz').value) || 20;
        const windowSec = this.rmsCapacity / hz;
        this.ctx.fillStyle = 'rgba(148, 163, 184, 0.6)';
        this.ctx.font = '10px ui-monospace, monospace';
        this.ctx.textAlign = 'left';
        this.ctx.fillText('-' + windowSec.toFixed(1) + 's', 4, h - 4);
        this.ctx.textAlign = 'right';
        this.ctx.fillText('now', w - 4, h - 4);
        this.ctx.textAlign = 'left';
        this.ctx.fillText(formatThousands(peak), 4, topPad + 8);
    }

    async flashFromUpload() {
        const fileInput = document.getElementById('pico-uf2-file');
        if (!fileInput.files.length) return;
        const form = new FormData();
        form.append('uf2', fileInput.files[0]);
        await this.runFlashStream('/api/pico/flash', {
            method: 'POST',
            body: form
        }, `Uploading ${fileInput.files[0].name}...`);
    }

    async flashBundled() {
        await this.runFlashStream('/api/pico/flash-bundled', {
            method: 'POST'
        }, 'Flashing bundled firmware...');
    }

    async runFlashStream(url, fetchInit, headerLine) {
        const log = document.getElementById('pico-flash-log');
        const uploadBtn = document.getElementById('pico-flash-btn');
        const bundledBtn = document.getElementById('pico-flash-bundled-btn');

        const wasStreaming = this.rmsSource !== null;
        if (wasStreaming) this.stopRmsStream();
        this.flashInProgress = true;

        log.textContent = `${headerLine}\n`;
        uploadBtn.disabled = true;
        bundledBtn.disabled = true;

        try {
            const resp = await fetch(url, fetchInit);
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
            uploadBtn.disabled = document.getElementById('pico-uf2-file').files.length === 0;
            bundledBtn.disabled = false;
            this.flashInProgress = false;
            if (wasStreaming) {
                setTimeout(() => {
                    if (!this.rmsPaused && !document.hidden) this.startRmsStream();
                }, 2500);
            }
        }
    }

    flashMessage(text, type) {
        const badge = document.getElementById('pico-conn-state');
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
