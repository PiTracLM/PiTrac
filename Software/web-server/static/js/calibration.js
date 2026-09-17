/* global requireStrobeSafe, escapeHtml, toast, confirmDialog, api, formatNumber, distortionCalibration */

const CAMERAS = ['camera1', 'camera2'];
const CAMERA_LABELS = { camera1: 'Camera 1', camera2: 'Camera 2' };
const POSITION_DURATION = { camera1: 'about 30 s', camera2: 'about 2 minutes' };
const SECTIONS = ['strobe', 'lens', 'position'];
const LOCK_NOTES = {
    strobe: 'Calibrate the strobe first. It is the first row in the checklist above.',
    lens: (camera) => `Calibrate the lens for ${CAMERA_LABELS[camera].toLowerCase()} first.`,
};

const BADGES = {
    done: ['Done', 'badge-success'],
    stale: ['Redo recommended', 'badge-warning'],
    running: ['Running', 'badge-info'],
    needs: ['Needs calibration', 'badge-warning'],
    factory: ['Factory default', 'badge-ghost'],
    strobe: ['Do the strobe first', 'badge-ghost'],
    lens: ['Do the lens first', 'badge-ghost'],
};

function shortDate(iso) {
    const date = new Date(iso);
    if (!iso || Number.isNaN(date.getTime())) return '';
    const sameYear = date.getFullYear() === new Date().getFullYear();
    return date.toLocaleDateString(undefined, { month: 'short', day: 'numeric', year: sameYear ? undefined : 'numeric' });
}

class CalibrationPage {
    constructor() {
        this.setup = null;
        this.status = null;
        this.section = null;
        this.strobeRunning = false;
        this.strobePollingTimer = null;
        this.positionRunning = false;
        this.positionCamera = null;
        this.positionStopped = false;
        this.positionPollTimer = null;
        this.redoFailures = {};

        window.addEventListener('hashchange', () => this.showSection(location.hash.slice(1)));
        window.addEventListener('pagehide', () => this.cleanup());
        document.querySelectorAll('input[name="position-camera"]').forEach(input => {
            input.addEventListener('change', () => this.renderPosition());
        });
        this.init();
    }

    async init() {
        await this.refresh();
        this.loadStrobeSettings();

        if (this.strobeRunning) {
            this.open('strobe');
            this.showStrobeRunning();
            this.pollStrobeStatus();
            return;
        }
        const lensCamera = CAMERAS.find(c => distortionCalibration.isActive(this.status?.distortion?.[c]));
        if (lensCamera) {
            this.open('lens', lensCamera);
            return;
        }
        const positionCamera = CAMERAS.find(c => this.status?.[c]?.status === 'calibrating');
        if (positionCamera) {
            this.open('position', positionCamera);
            this.setPositionRunning(true);
            this.showPositionProgress(positionCamera, true);
            return;
        }
        this.showSection(location.hash.slice(1));
    }

    cleanup() {
        clearTimeout(this.strobePollingTimer);
        clearInterval(this.positionPollTimer);
        distortionCalibration.leave();
    }

    async refresh() {
        const [setup, status, strobe] = await Promise.all([
            api('/api/setup/status'),
            api('/api/calibration/status'),
            api('/api/strobe-calibration/status'),
        ].map(p => p.catch(() => null)));
        if (setup) this.setup = setup;
        if (status) this.status = status;
        if (strobe) this.strobeRunning = strobe.state === 'calibrating';
        this.renderChecklist();
    }

    // -- Checklist --

    lockReason(section, camera) {
        const s = this.setup;
        if (!s || section === 'strobe') return null;
        if (s.strobe.required && !s.strobe.safe) return 'strobe';
        if (section === 'position' && !s.cameras[camera].lens_calibrated) return 'lens';
        return null;
    }

    lockNote(section, cameras) {
        const reasons = cameras.map(c => [c, this.lockReason(section, c)]).filter(([, r]) => r);
        if (!reasons.length) return null;
        if (reasons.some(([, r]) => r === 'strobe')) return LOCK_NOTES.strobe;
        return reasons.map(([c]) => LOCK_NOTES.lens(c)).join(' ');
    }

    rowState(section, camera) {
        const s = this.setup;
        if (section === 'strobe') {
            if (this.strobeRunning) return 'running';
            return s.strobe.safe ? 'done' : 'needs';
        }
        const running = section === 'lens'
            ? distortionCalibration.isActive(this.status?.distortion?.[camera])
            : this.status?.[camera]?.status === 'calibrating';
        if (running) return 'running';
        const locked = this.lockReason(section, camera);
        if (locked) return locked;
        const cam = s.cameras[camera];
        if (section === 'lens') return cam.lens_calibrated ? 'done' : 'factory';
        if (!cam.position_calibrated) return 'factory';
        return this.lensNewerThanPosition(cam) ? 'stale' : 'done';
    }

    lensNewerThanPosition(cam) {
        return !!(cam.lens_updated_at && cam.position_updated_at
            && new Date(cam.lens_updated_at) > new Date(cam.position_updated_at));
    }

    updatedAt(section, camera) {
        if (section === 'strobe') return this.setup.strobe.updated_at;
        return this.setup.cameras[camera][`${section}_updated_at`];
    }

    runEnded(section, camera, failure) {
        const rowId = camera ? `${section}-${camera}` : section;
        const wasDone = section === 'strobe' ? this.setup?.strobe.safe : this.setup?.cameras[camera][`${section}_calibrated`];
        if (failure && wasDone) {
            this.redoFailures[rowId] = section === 'strobe'
                ? `Redo failed. ${failure}`
                : `Redo failed. Your previous calibration is still in use. ${failure}`;
        } else {
            delete this.redoFailures[rowId];
        }
    }

    renderChecklist() {
        if (!this.setup) return;
        document.getElementById('check-strobe').hidden = !this.setup.strobe.required;

        let nextFound = false;
        let allDone = true;
        document.querySelectorAll('#checklist > li:not([hidden])').forEach((row, i) => {
            const rowId = row.id.slice('check-'.length);
            const [section, camera] = rowId.split('-');
            const state = this.rowState(section, camera);
            const locked = state === 'strobe' || state === 'lens';
            const primary = !nextFound && !locked && state !== 'done';
            if (primary) nextFound = true;
            if (state !== 'done') allDone = false;

            const [label, color] = BADGES[state];
            const date = state === 'done' ? shortDate(this.updatedAt(section, camera)) : '';
            row.querySelector('.check-num').textContent = i + 1;
            const badge = row.querySelector('.check-badge');
            badge.className = `check-badge badge badge-sm badge-soft ${color} mt-1`;
            badge.textContent = date ? `${label}, ${date}` : label;

            const failure = state === 'running' ? null : this.redoFailures[rowId];
            const note = row.querySelector('.check-note');
            note.textContent = failure || (state === 'stale' ? 'The lens was recalibrated after this.' : '');
            note.className = `check-note text-sm mt-1 ${failure ? 'text-error' : 'text-warning'}`;
            note.hidden = !note.textContent;

            const btn = row.querySelector('.check-btn');
            btn.className = `check-btn btn btn-sm ${primary ? 'btn-primary' : 'btn-ghost'}`;
            btn.textContent = { done: 'Redo', stale: 'Redo', running: 'View' }[state] || 'Calibrate';
            btn.disabled = locked;
        });
        document.getElementById('checklist-done').hidden = !allDone;

        this.renderStrobeState();
        this.renderPosition();
        distortionCalibration.renderLock();
    }

    // -- Sections --

    open(section, camera) {
        const input = camera && document.querySelector(`input[name="${section}-camera"][value="${camera}"]`);
        if (input && !input.disabled) input.checked = true;
        if (location.hash !== `#${section}`) window.history.replaceState(null, '', `#${section}`);
        this.showSection(section);
        this.renderPosition();
    }

    showSection(name) {
        if (name === 'strobe' && this.setup && !this.setup.strobe.required) return;
        if (!SECTIONS.includes(name)) return;
        if (this.section === 'lens' && name !== 'lens') distortionCalibration.leave();
        this.section = name;
        SECTIONS.forEach(s => { document.getElementById(s).hidden = s !== name; });
        if (name === 'lens') distortionCalibration.enter();
        document.getElementById(name).scrollIntoView({ behavior: 'smooth', block: 'start' });
    }

    // -- Position --

    positionCameras() {
        const choice = document.querySelector('input[name="position-camera"]:checked').value;
        return choice === 'both' ? CAMERAS : [choice];
    }

    renderPosition() {
        const note = this.positionRunning ? null : this.lockNote('position', this.positionCameras());
        const lockEl = document.getElementById('position-locked');
        lockEl.hidden = !note;
        lockEl.textContent = note || '';
        document.getElementById('position-start-btn').hidden = this.positionRunning || !!note;
        document.getElementById('position-stop-btn').hidden = !this.positionRunning;
        document.getElementById('position-progress').hidden = !this.positionRunning;
        document.querySelectorAll('input[name="position-camera"]').forEach(i => { i.disabled = this.positionRunning; });
    }

    setPositionRunning(running) {
        this.positionRunning = running;
        if (running) {
            this.positionStopped = false;
            document.getElementById('position-results').replaceChildren();
        } else {
            clearInterval(this.positionPollTimer);
            this.positionCamera = null;
        }
        this.renderPosition();
    }

    async startPosition() {
        if (!(await requireStrobeSafe())) return;
        this.setPositionRunning(true);
        for (const camera of this.positionCameras()) {
            if (this.positionStopped) break;
            this.showPositionProgress(camera, false);
            let result;
            try {
                result = await api(`/api/calibration/auto/${camera}`, { method: 'POST' });
            } catch (err) {
                result = { status: 'error', message: err.message };
            }
            const ok = await this.showPositionResult(camera, result.status === 'success', result.message);
            if (!ok) break;
        }
        this.setPositionRunning(false);
        this.refresh();
    }

    // The auto calibration POST blocks until the run ends. The status poll only feeds the message,
    // unless the page was reloaded mid-run and there is no POST to wait on.
    showPositionProgress(camera, reattached) {
        this.positionCamera = camera;
        document.getElementById('position-progress-title').textContent =
            `Calibrating ${CAMERA_LABELS[camera].toLowerCase()}, ${POSITION_DURATION[camera]}`;
        document.getElementById('position-progress-message').textContent = '';
        clearInterval(this.positionPollTimer);
        this.positionPollTimer = setInterval(async () => {
            const status = await api('/api/calibration/status').catch(() => null);
            if (!status) return;
            this.status = status;
            this.renderChecklist();
            const st = status[camera] || {};
            if (reattached && st.status !== 'calibrating') {
                await this.showPositionResult(camera, st.status === 'completed', st.message);
                this.setPositionRunning(false);
                this.refresh();
                return;
            }
            if (st.message) document.getElementById('position-progress-message').textContent = st.message;
        }, 2000);
    }

    async showPositionResult(camera, ok, message) {
        clearInterval(this.positionPollTimer);
        this.runEnded('position', camera, ok || this.positionStopped ? null : message || 'Calibration failed');
        const label = CAMERA_LABELS[camera];
        const box = document.createElement('div');
        if (this.positionStopped) {
            box.className = 'alert alert-soft';
            box.textContent = `${label}: stopped.`;
        } else if (ok) {
            const data = await api('/api/calibration/data').catch(() => null);
            const cam = (data && data[camera]) || {};
            const angles = Array.isArray(cam.angles) ? cam.angles.map(Number) : [];
            const tilt = angles.length > 1 && !Number.isNaN(angles[1])
                ? ` Camera tilted ${Math.abs(angles[1]).toFixed(0)} degrees ${angles[1] < 0 ? 'down' : 'up'}.`
                : '';
            box.className = 'alert alert-success alert-soft';
            box.innerHTML = `
                <div class="min-w-0">
                    <div class="font-semibold">${label}</div>
                    <div>Done.${tilt}</div>
                    <details class="mt-1 text-sm">
                        <summary class="cursor-pointer opacity-70">Details</summary>
                        <div>Focal length: ${formatNumber(cam.focal_length, 3)} mm</div>
                        <div>Angles: ${angles.map(a => formatNumber(a, 2)).join(', ') || '--'} degrees (side to side, up and down)</div>
                    </details>
                </div>`;
        } else {
            box.className = 'alert alert-error alert-soft';
            box.innerHTML = `
                <div class="min-w-0">
                    <div class="font-semibold">${label} could not be calibrated</div>
                    <div class="text-sm">Check that the ball is in place and the camera can see it, then try again.</div>
                    ${message ? `<div class="text-sm opacity-70 mt-1">${escapeHtml(message)}</div>` : ''}
                    <a href="/logs" class="link text-sm">Open logs</a>
                </div>`;
        }
        document.getElementById('position-results').append(box);
        return ok && !this.positionStopped;
    }

    async stopPosition() {
        const camera = this.positionCamera;
        const ok = await confirmDialog({
            title: 'Stop position calibration?',
            body: `This stops the run on ${CAMERA_LABELS[camera].toLowerCase()}.`,
            confirmLabel: 'Stop',
            danger: true,
        });
        if (!ok) return;
        this.positionStopped = true;
        try {
            await api('/api/calibration/stop', { method: 'POST', body: { camera, kind: 'ball' } });
        } catch (err) {
            toast(err.message, 'error');
        }
    }

    // -- Strobe --

    renderStrobeState() {
        if (!this.setup) return;
        const safe = this.setup.strobe.safe;
        document.getElementById('strobe-state').textContent =
            this.strobeRunning ? 'Strobe: calibrating' : safe ? 'Strobe: calibrated' : 'Strobe: needs calibration';
        const btn = document.getElementById('strobe-calibrate-btn');
        if (!this.strobeRunning) btn.textContent = safe ? 'Recalibrate' : 'Calibrate';
    }

    async loadStrobeSettings() {
        try {
            const settings = await api('/api/strobe-calibration/settings');
            document.getElementById('strobe-saved-dac').textContent =
                settings.dac_setting == null ? 'Not set' : this.formatDac(settings.dac_setting);
        } catch (error) {
            console.error('Error loading strobe settings:', error);
        }
    }

    formatDac(value) {
        return '0x' + parseInt(value).toString(16).toUpperCase().padStart(2, '0');
    }

    showStrobeRunning() {
        this.strobeRunning = true;
        const btn = document.getElementById('strobe-calibrate-btn');
        btn.disabled = true;
        btn.textContent = 'Calibrating';
        document.getElementById('strobe-cancel-btn').hidden = false;
        document.getElementById('strobe-progress-area').hidden = false;
        document.getElementById('strobe-result-area').hidden = true;
        const bar = document.getElementById('strobe-progress-fill');
        bar.value = 0;
        bar.classList.remove('progress-error');
        document.getElementById('strobe-progress-message').textContent = 'Starting';
        this.renderChecklist();
    }

    async startStrobeCalibration() {
        const ledType = document.getElementById('strobe-led-type').value;
        this.showStrobeRunning();
        try {
            await api('/api/strobe-calibration/start', {
                method: 'POST',
                body: { led_type: ledType, target_current: ledType === 'v3' ? 10.0 : 9.0, overwrite: true },
            });
            this.pollStrobeStatus();
        } catch (error) {
            this.finishStrobe();
            document.getElementById('strobe-progress-message').textContent = error.message;
        }
    }

    pollStrobeStatus() {
        this.strobePollingTimer = setTimeout(async () => {
            let status;
            try {
                status = await api('/api/strobe-calibration/status');
            } catch (error) {
                console.error('Error polling strobe status:', error);
                this.pollStrobeStatus();
                return;
            }
            if (status.progress !== undefined) {
                document.getElementById('strobe-progress-fill').value = parseFloat(status.progress);
            }
            if (status.message) {
                document.getElementById('strobe-progress-message').textContent = status.message;
            }

            if (status.state === 'complete' || status.state === 'completed') {
                this.onStrobeCalibrationDone(status);
            } else if (status.state === 'failed' || status.state === 'error') {
                this.onStrobeCalibrationFailed(status);
            } else if (status.state === 'cancelled') {
                this.runEnded('strobe', '', null);
                this.finishStrobe();
                document.getElementById('strobe-progress-message').textContent = 'Cancelled';
            } else {
                this.pollStrobeStatus();
            }
        }, 500);
    }

    async finishStrobe() {
        this.strobeRunning = false;
        document.getElementById('strobe-calibrate-btn').disabled = false;
        const cancelBtn = document.getElementById('strobe-cancel-btn');
        cancelBtn.hidden = true;
        cancelBtn.disabled = false;
        cancelBtn.textContent = 'Cancel';
        this.loadStrobeSettings();
        await this.refresh();
    }

    showStrobeResult(success, message) {
        document.getElementById('strobe-result-card').style.borderColor =
            success ? 'var(--color-success)' : 'var(--color-error)';
        document.getElementById('strobe-result-title').textContent = success ? 'Calibrated' : 'Calibration failed';
        document.getElementById('strobe-result-message').textContent = message;
        document.getElementById('strobe-result-area').hidden = false;
    }

    onStrobeCalibrationDone(status) {
        document.getElementById('strobe-progress-fill').value = 100;
        document.getElementById('strobe-result-dac').textContent =
            status.dac_setting == null ? '--' : this.formatDac(status.dac_setting);
        document.getElementById('strobe-result-current').textContent = `${formatNumber(status.led_current, 2)} A`;
        document.getElementById('strobe-result-ldo').textContent = `${formatNumber(status.ldo_voltage, 2)} V`;
        this.runEnded('strobe', '', null);
        this.showStrobeResult(true, 'The strobe is ready. Next, calibrate the lenses.');
        this.finishStrobe();
    }

    onStrobeCalibrationFailed(status) {
        const bar = document.getElementById('strobe-progress-fill');
        bar.value = 100;
        bar.classList.add('progress-error');
        this.runEnded('strobe', '', status.message || 'Calibration failed');
        this.showStrobeResult(false, status.message || 'Calibration failed');
        this.finishStrobe();
    }

    async cancelStrobeCalibration() {
        const cancelBtn = document.getElementById('strobe-cancel-btn');
        cancelBtn.disabled = true;
        cancelBtn.textContent = 'Cancelling';
        try {
            await api('/api/strobe-calibration/cancel', { method: 'POST' });
        } catch (error) {
            toast(error.message, 'error');
            cancelBtn.disabled = false;
            cancelBtn.textContent = 'Cancel';
        }
    }
}

const calibration = new CalibrationPage();
