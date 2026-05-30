const STREAM_STALE_MS = 3000;
const RMS_STREAM_MIN_FW = [0, 5, 0];
const NOISE_FLOOR_EASE = 0.25;
const AXIS_EASE = 0.2;
const AXIS_HEADROOM = 1.4;
const SET_ABOVE_NOISE_FACTOR = 1.5;
const SLIDER_RANGE_FACTOR = 3;
const THIN_MARGIN_RATIO = 1.25;
const MARGIN_CLASS = 'text-[11px]';

function parseFwVersion(fw) {
    if (typeof fw !== 'string') return null;
    const match = fw.match(/(\d+)\.(\d+)\.(\d+)/);
    if (!match) return null;
    return [Number(match[1]), Number(match[2]), Number(match[3])];
}

function fwAtLeast(fw, minimum) {
    const parsed = parseFwVersion(fw);
    if (!parsed) return false;
    for (let i = 0; i < minimum.length; i++) {
        if (parsed[i] > minimum[i]) return true;
        if (parsed[i] < minimum[i]) return false;
    }
    return true;
}

function formatThousands(value) {
    const v = Number(value) || 0;
    if (v >= 1_000_000) return (v / 1_000_000).toFixed(2) + 'M';
    if (v >= 1_000) return (v / 1_000).toFixed(1) + 'k';
    return String(Math.round(v));
}

function marginTier(threshold, floor) {
    if (!floor || !threshold) return 'unknown';
    if (threshold < floor) return 'below';
    if (threshold / floor < THIN_MARGIN_RATIO) return 'thin';
    return 'ok';
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
        this.toastTimer = null;
        this.noiseFloor = 0;
        this.axisTop = 0;
        this.lastEventCount = null;
        this.triggerFlashUntil = 0;

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
            this.updateThresholdReadout(thresholdSlider.value);
            this.drawRms();
        });
        thresholdSlider.addEventListener('change', () => this.saveThreshold());
        document.getElementById('pico-threshold-auto').addEventListener('click', () => this.applyAutoThreshold());

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
        document.getElementById('pico-board').textContent = data.board || '--';
        document.getElementById('pico-device').textContent = data.device || '/dev/ttyACM0';
        document.getElementById('pico-vbus').textContent = (data.vbus === 1 || data.vbus === true) ? 'present' : 'absent';
        const eventCount = typeof data.event_count === 'number' ? data.event_count : null;
        document.getElementById('pico-event-count').textContent = eventCount ?? '--';
        if (eventCount !== null) {
            if (this.lastEventCount !== null && eventCount > this.lastEventCount) {
                this.flashTrigger();
            }
            this.lastEventCount = eventCount;
        }

        const armed = data.armed === 1 || data.armed === true;
        const toggle = document.getElementById('pico-armed-toggle');
        if (document.activeElement !== toggle) toggle.checked = armed;
        document.getElementById('pico-armed-state').textContent = armed ? 'armed' : 'disarmed';

        const thresholdInput = document.getElementById('pico-threshold');
        if (!this.userTouchedThreshold
            && document.activeElement !== thresholdInput
            && typeof data.threshold === 'number') {
            this.syncSliderRange(data.threshold);
            thresholdInput.value = data.threshold;
            this.updateThresholdReadout(data.threshold);
            this.drawRms();
        }

        const minInput = document.getElementById('pico-min-inter-shot');
        if (document.activeElement !== minInput && typeof data.min_inter_shot_ms === 'number') {
            minInput.value = data.min_inter_shot_ms;
        }
    }

    updateThresholdReadout(value) {
        const slider = document.getElementById('pico-threshold');
        const v = Number(value) || 0;
        document.getElementById('pico-threshold-value').textContent = v.toLocaleString();
        slider.setAttribute('aria-valuetext', `${v} raw RMS, ${this.marginPhrase(v)}`);
        this.updateMarginReadout(v);
    }

    marginPhrase(threshold) {
        if (!threshold) return 'not set';
        if (!this.noiseFloor) return 'noise floor not measured';
        const ratio = threshold / this.noiseFloor;
        const tier = marginTier(threshold, this.noiseFloor);
        if (tier === 'below') return `below noise floor (${ratio.toFixed(2)}×)`;
        if (tier === 'thin') return `thin margin, ${ratio.toFixed(1)}× noise floor`;
        return `${ratio.toFixed(1)}× above noise floor`;
    }

    updateMarginReadout(threshold) {
        const el = document.getElementById('pico-threshold-margin');
        if (!el) return;
        const floorText = this.noiseFloor ? formatThousands(this.noiseFloor) : '--';
        const tier = marginTier(threshold, this.noiseFloor);
        if (tier === 'unknown') {
            el.textContent = threshold
                ? `noise floor ${floorText} · no live signal`
                : `noise floor ${floorText} · not set`;
            el.className = `${MARGIN_CLASS} text-warning`;
            return;
        }
        const ratio = threshold / this.noiseFloor;
        if (tier === 'below') {
            el.textContent = `noise ${floorText} · below floor (${ratio.toFixed(2)}×)`;
            el.className = `${MARGIN_CLASS} text-error`;
        } else if (tier === 'thin') {
            el.textContent = `noise ${floorText} · thin margin (${ratio.toFixed(1)}×)`;
            el.className = `${MARGIN_CLASS} text-warning`;
        } else {
            el.textContent = `noise ${floorText} · ${ratio.toFixed(1)}× margin`;
            el.className = `${MARGIN_CLASS} opacity-70`;
        }
    }

    updateAutoButton() {
        const btn = document.getElementById('pico-threshold-auto');
        if (btn) btn.disabled = !(this.noiseFloor > 0);
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
        const ok = await this.postConfig({ threshold: value });
        // Once the slider value is committed to the Pico, drop the latch so a
        // later server-side change can re-sync the control.
        if (ok) this.userTouchedThreshold = false;
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
                return false;
            }
            // The endpoint returns HTTP 200 even when the firmware echo check
            // fails, so surface any per-field reject carried in `applied`.
            const rejected = this.collectAppliedRejects(data.applied);
            if (rejected) {
                this.flashMessage(rejected, 'error');
                return false;
            }
            this.flashMessage('Saved', 'success');
            return true;
        } catch (err) {
            this.flashMessage(err.message, 'error');
            return false;
        }
    }

    collectAppliedRejects(applied) {
        if (!applied || typeof applied !== 'object') return null;
        const messages = [];
        Object.keys(applied).forEach((key) => {
            const result = applied[key];
            if (result && typeof result === 'object' && result.ok === false) {
                messages.push(result.error || `${key} rejected by firmware`);
            }
        });
        return messages.length ? messages.join('; ') : null;
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
        if (parseFwVersion(fw) && !fwAtLeast(fw, RMS_STREAM_MIN_FW)) {
            this.setRmsHint(`No events. Pico fw=${fw} predates CFG STREAM_RMS - flash 0.5.0 or newer via the Firmware card below.`);
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
        document.getElementById('pico-rms-latest').textContent = sample.value.toLocaleString();
        document.getElementById('pico-rms-peak').textContent = this.rmsPeak.toLocaleString();

        this.updateNoiseFloor();
        this.updateAxis();
        this.syncSliderRange();
        this.updateAutoButton();
        this.updateMarginReadout(Number(document.getElementById('pico-threshold').value) || 0);
        this.drawRms();
    }

    updateNoiseFloor() {
        const n = this.rmsSamples.length;
        if (!n) return;
        const sorted = this.rmsSamples.map((s) => s.value).sort((a, b) => a - b);
        const median = sorted[Math.floor(n / 2)];
        // Median rides through the room hum and ignores the odd strike; easing
        // keeps the floor from jittering the axis and slider every frame.
        this.noiseFloor = this.noiseFloor
            ? this.noiseFloor + (median - this.noiseFloor) * NOISE_FLOOR_EASE
            : median;
    }

    updateAxis() {
        const threshold = Number(document.getElementById('pico-threshold').value) || 0;
        const target = Math.max(Math.max(threshold, this.noiseFloor * 1.15) * AXIS_HEADROOM, 1000);
        this.axisTop = this.axisTop ? this.axisTop + (target - this.axisTop) * AXIS_EASE : target;
    }

    syncSliderRange(mustFit = 0) {
        const slider = document.getElementById('pico-threshold');
        const threshold = Number(slider.value) || 0;
        const target = Math.max(this.noiseFloor * SLIDER_RANGE_FACTOR, threshold * 1.25, mustFit * 1.1, 100000);
        const targetMax = Math.ceil(target / 100000) * 100000;
        const currentMax = Number(slider.max) || 100000;
        const idle = !this.userTouchedThreshold && document.activeElement !== slider;
        if (targetMax > currentMax || (idle && currentMax > targetMax * 1.6)) {
            slider.max = String(targetMax);
            slider.step = String(Math.max(1000, Math.round(targetMax / 200 / 1000) * 1000));
            document.getElementById('pico-threshold-max').textContent = formatThousands(targetMax);
        }
    }

    applyAutoThreshold() {
        if (!(this.noiseFloor > 0)) return;
        const slider = document.getElementById('pico-threshold');
        this.userTouchedThreshold = true;
        const target = Math.round(this.noiseFloor * SET_ABOVE_NOISE_FACTOR);
        this.syncSliderRange(target);
        slider.value = target;
        // Read the slider back so the readout and the committed POST agree with
        // the value the range input snapped to its step.
        const committed = Number(slider.value);
        this.updateThresholdReadout(committed);
        this.updateAxis();
        this.drawRms();
        this.saveThreshold();
    }

    flashTrigger() {
        this.triggerFlashUntil = Date.now() + 1000;
        this.flashMessage('Shot detected', 'success');
        this.drawRms();
        setTimeout(() => this.drawRms(), 1050);
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
        const hasData = this.rmsSamples.length > 0;

        const threshold = Number(document.getElementById('pico-threshold').value) || 0;
        // Ceiling is pinned to the floor and the threshold, never to the loudest
        // sample, so a strike clips at the top instead of crushing the noise band.
        const top = Math.max(this.axisTop, threshold * 1.15, this.noiseFloor * 1.3, 1000);
        const yOf = (v) => topPad + (1 - Math.min(Math.max(v, 0), top) / top) * usableH;

        this.ctx.font = '10px ui-monospace, monospace';

        if (threshold > 0 && this.noiseFloor > 0) {
            const yThreshold = yOf(threshold);
            const yFloor = yOf(this.noiseFloor);
            const tier = marginTier(threshold, this.noiseFloor);
            this.ctx.fillStyle = tier === 'below'
                ? 'rgba(239, 68, 68, 0.14)'
                : tier === 'thin'
                    ? 'rgba(245, 158, 11, 0.14)'
                    : 'rgba(34, 197, 94, 0.12)';
            this.ctx.fillRect(0, Math.min(yThreshold, yFloor), w, Math.abs(yFloor - yThreshold));
        }

        if (this.noiseFloor > 0) {
            const yFloor = yOf(this.noiseFloor);
            this.ctx.strokeStyle = 'rgba(148, 163, 184, 0.5)';
            this.ctx.lineWidth = 1;
            this.ctx.beginPath();
            this.ctx.moveTo(0, yFloor);
            this.ctx.lineTo(w, yFloor);
            this.ctx.stroke();
            this.ctx.fillStyle = 'rgba(148, 163, 184, 0.85)';
            this.ctx.textAlign = 'left';
            this.ctx.fillText('noise ' + formatThousands(this.noiseFloor), 4, Math.min(yFloor + 11, h - bottomPad));
        }

        if (threshold > 0) {
            const yThreshold = yOf(threshold);
            this.ctx.strokeStyle = '#f97316';
            this.ctx.lineWidth = 1.5;
            this.ctx.setLineDash([5, 4]);
            this.ctx.beginPath();
            this.ctx.moveTo(0, yThreshold);
            this.ctx.lineTo(w, yThreshold);
            this.ctx.stroke();
            this.ctx.setLineDash([]);
            this.ctx.fillStyle = '#f97316';
            this.ctx.textAlign = 'right';
            this.ctx.fillText(formatThousands(threshold), w - 4, Math.max(yThreshold - 4, 10));
        }

        if (this.rmsSamples.length >= 2) {
            const values = this.rmsSamples.map((s) => s.value);
            this.ctx.strokeStyle = '#3b82f6';
            this.ctx.lineWidth = 2;
            this.ctx.beginPath();
            values.forEach((v, i) => {
                const x = (i / (this.rmsCapacity - 1)) * w;
                const y = yOf(v);
                if (i === 0) this.ctx.moveTo(x, y); else this.ctx.lineTo(x, y);
            });
            this.ctx.stroke();

            this.ctx.fillStyle = '#ef4444';
            values.forEach((v, i) => {
                if (v <= top) return;
                const x = (i / (this.rmsCapacity - 1)) * w;
                this.ctx.beginPath();
                this.ctx.moveTo(x, topPad);
                this.ctx.lineTo(x - 3, topPad + 5);
                this.ctx.lineTo(x + 3, topPad + 5);
                this.ctx.closePath();
                this.ctx.fill();
            });
        }

        this.ctx.fillStyle = 'rgba(148, 163, 184, 0.6)';
        this.ctx.textAlign = 'left';
        if (hasData) this.ctx.fillText(formatThousands(top), 4, topPad + 8);
        const hz = Number(document.getElementById('pico-rms-hz').value) || 20;
        const windowSec = this.rmsCapacity / hz;
        this.ctx.fillText('-' + windowSec.toFixed(1) + 's', 4, h - 4);
        this.ctx.textAlign = 'right';
        this.ctx.fillText('now', w - 4, h - 4);

        if (this.triggerFlashUntil > Date.now()) {
            this.ctx.fillStyle = 'rgba(34, 197, 94, 0.18)';
            this.ctx.fillRect(0, 0, w, h);
            this.ctx.fillStyle = '#22c55e';
            this.ctx.font = '12px ui-monospace, monospace';
            this.ctx.textAlign = 'center';
            this.ctx.fillText('SHOT DETECTED', w / 2, h / 2);
        }
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
        const toast = document.getElementById('pico-toast');
        const msg = document.getElementById('pico-toast-msg');
        if (!toast || !msg) return;
        msg.textContent = text;
        msg.className = `alert ${type === 'error' ? 'alert-error' : 'alert-success'}`;
        toast.classList.remove('hidden');
        if (this.toastTimer) clearTimeout(this.toastTimer);
        this.toastTimer = setTimeout(() => {
            toast.classList.add('hidden');
        }, 2500);
    }
}

window.PicoController = PicoController;
const pico = new PicoController();
window.pico = pico;
