/* global calibration, escapeHtml, toast, confirmDialog, api, openSocket */
/* exported distortionCalibration */

const distortionCalibration = {
    camera: null,
    pollInterval: null,
    feed: null,
    feedPending: null,
    preview: null,
    previewMode: 'side_by_side',

    init() {
        this._showOnLoad('distortion-feed', 'distortion-feed-placeholder');
        this._showOnLoad('undistort-feed', 'undistort-placeholder');
        document.querySelectorAll('input[name="lens-camera"]').forEach(input => {
            input.addEventListener('change', () => {
                this._stopPreview();
                this.showSetup();
            });
        });
    },

    isActive(status) {
        return ['distortion_calibrating', 'stopping'].includes(status?.status);
    },

    selectedCamera() {
        return document.querySelector('input[name="lens-camera"]:checked').value;
    },

    _setPicker(camera, disabled) {
        document.querySelectorAll('input[name="lens-camera"]').forEach(input => {
            if (camera) input.checked = input.value === camera;
            input.disabled = disabled;
        });
    },

    // A run keeps its status poll while the section is closed, so it can finish there; entering only reopens the feed.
    async enter() {
        if (this.camera) {
            if (!this.feed) this._startFeed(this.camera).catch(() => {});
            return;
        }
        await calibration.refresh();
        const running = ['camera1', 'camera2'].find(c => this.isActive(calibration.status?.distortion?.[c]));
        if (!running) {
            this.showSetup();
            return;
        }
        const attached = this.attach(running);
        this.startStatusPolling();
        try {
            await attached;
        } catch (error) {
            if (!error.cancelled) document.getElementById('distortion-status').textContent = error.message;
        }
    },

    leave() {
        this._stopFeed();
        this._stopPreview();
    },

    renderLock() {
        const note = calibration.lockNote('lens', ['camera1', 'camera2']);
        const lockEl = document.getElementById('lens-locked');
        lockEl.hidden = !note;
        lockEl.textContent = note || '';
        document.getElementById('lens-start-btn').hidden = !!note;
        this._renderPreviewButton();
    },

    _renderPreviewButton() {
        const calibrated = calibration.setup?.cameras?.[this.selectedCamera()]?.lens_calibrated;
        document.getElementById('undistort-preview-btn').hidden = !calibrated;
    },

    showSetup() {
        this._setPicker(null, false);
        document.getElementById('lens-setup').hidden = false;
        document.getElementById('lens-progress').hidden = true;
        this.renderLock();
    },

    // Opens the live feed and progress view for a run. The feed must be open before the run starts:
    // the run reads its frames from this feed.
    async attach(camera) {
        this.camera = camera;
        this._setPicker(camera, true);
        this._stopPreview();
        document.getElementById('lens-setup').hidden = true;
        document.getElementById('lens-progress').hidden = false;
        document.getElementById('lens-progress-title').textContent =
            `Calibrating the lens on ${camera === 'camera1' ? 'camera 1' : 'camera 2'}`;
        document.getElementById('distortion-status').textContent = 'Starting';
        document.getElementById('distortion-hint').textContent = '';
        this._initCoverageGrid();
        this._updateProgress({ progress: 0, images_captured: 0, target_images: 40,
            requirements: { coverage: 0, coverage_target: 0.8, tilt: 0, tilt_target: 0.4,
                bins: { small: 0, medium: 0, large: 0 }, bin_target: 3 } });
        await this._startFeed(camera);
        this.log('Camera feed connected');
    },

    async startCalibration() {
        const camera = this.selectedCamera();
        document.getElementById('lens-result').replaceChildren();
        document.getElementById('distortion-log-content').textContent = '';
        this.log(`Starting lens calibration for ${camera === 'camera1' ? 'camera 1' : 'camera 2'}`);
        try {
            await this.attach(camera);
            const result = await api(`/api/calibration/distortion/${camera}`, { method: 'POST', body: { target_images: 40 } });
            if (result.status === 'error') throw new Error(result.message);
            this.log('Calibration started');
            this.startStatusPolling();
        } catch (error) {
            if (error.cancelled) {
                this.camera = null;
                this.showSetup();
                return;
            }
            this.finish('error', error.message || 'Check that the camera is connected and not in use by another program.');
        }
    },

    startStatusPolling() {
        this._stopPolling();
        this.pollInterval = setInterval(async () => {
            const data = await api('/api/calibration/status').catch(() => null);
            if (!data || !this.camera) return;
            calibration.status = data;
            calibration.renderChecklist();

            const status = data.distortion[this.camera];
            if (!status) return;
            this._updateProgress(status);

            if (status.status === 'completed') {
                this.finish('success', status.message);
            } else if (status.status === 'failed' || status.status === 'error') {
                this.finish('error', status.message || 'Calibration failed');
            } else if (status.status === 'stopped') {
                this.finish('stopped');
            }
        }, 2000);
    },

    _updateProgress(status) {
        if (status.progress !== undefined) {
            document.getElementById('distortion-progress-bar').value = status.progress;
            document.getElementById('distortion-progress-pct').textContent = `${status.progress}%`;
        }
        if (status.message) {
            document.getElementById('distortion-status').textContent = status.message;
        }
        if (status.hint) {
            document.getElementById('distortion-hint').textContent = status.hint;
        }

        const r = status.requirements;
        if (r) {
            const pct = v => `${Math.round((v || 0) * 100)}%`;
            const target = status.target_images || 40;
            const captured = status.images_captured || 0;
            const bins = { near: r.bins.large || 0, mid: r.bins.medium || 0, far: r.bins.small || 0 };
            const binText = Object.entries(bins).map(([k, n]) => `${k} ${Math.min(n, r.bin_target)}/${r.bin_target}`).join(', ');
            this._setRequirement('req-images', captured >= target, `${captured} of ${target} good images`);
            this._setRequirement('req-coverage', r.coverage >= r.coverage_target,
                `Frame covered: ${pct(r.coverage)} of ${pct(r.coverage_target)}`);
            this._setRequirement('req-tilt', r.tilt >= r.tilt_target,
                `Tilted shots: ${pct(r.tilt)} of ${pct(r.tilt_target)}`);
            this._setRequirement('req-distance', Object.values(bins).every(n => n >= r.bin_target),
                `Distances: ${binText}`);
        }

        if (status.coverage && status.coverage.grid) {
            this._updateCoverageGrid(status.coverage);
        }
    },

    _setRequirement(id, satisfied, label) {
        const el = document.getElementById(id);
        el.className = `flex items-center gap-1.5 ${satisfied ? 'text-success' : 'opacity-70'}`;
        el.innerHTML = `<i data-lucide="${satisfied ? 'circle-check' : 'circle'}" class="icon-sm"></i><span>${escapeHtml(label)}</span>`;
        if (window.lucide && window.lucide.createIcons) {
            window.lucide.createIcons({ nodes: [el] });
        }
    },

    _initCoverageGrid() {
        const grid = document.getElementById('distortion-coverage-grid');
        grid.replaceChildren();
        for (let i = 0; i < 9; i++) {
            const cell = document.createElement('div');
            cell.className = 'coverage-cell';
            cell.dataset.count = '0';
            grid.appendChild(cell);
        }
    },

    _updateCoverageGrid(coverage) {
        const cells = document.getElementById('distortion-coverage-grid').children;
        coverage.grid.flat().forEach((count, idx) => {
            if (cells[idx]) cells[idx].dataset.count = String(Math.min(count, 3));
        });
    },

    async finish(outcome, message = '') {
        this._stopPolling();
        this._stopFeed();
        const camera = this.camera;
        calibration.runEnded('lens', camera, outcome === 'error' ? message || 'Calibration failed' : null);
        this.camera = null;
        if (calibration.section !== 'lens' && outcome !== 'stopped') {
            const label = camera === 'camera1' ? 'camera 1' : 'camera 2';
            toast(outcome === 'success' ? `Lens calibration for ${label} finished` : `Lens calibration for ${label} did not finish`,
                outcome === 'success' ? 'success' : 'error', { actionLabel: 'View', onAction: () => calibration.open('lens', camera) });
        }
        this.log(outcome === 'success' ? 'Calibration complete' : `Calibration ended: ${message || outcome}`);

        const result = document.getElementById('lens-result');
        if (outcome === 'success') {
            result.innerHTML = `
                <div class="alert alert-success alert-soft">
                    <div>
                        <div class="font-semibold">Lens calibrated and saved</div>
                        <div class="text-sm">${escapeHtml(message)}</div>
                        <div class="text-sm opacity-70 mt-1">Use Preview to check that straight lines look straight.</div>
                    </div>
                </div>`;
        } else if (outcome === 'error') {
            result.innerHTML = `
                <div class="alert alert-error alert-soft">
                    <div>
                        <div class="font-semibold">Lens calibration did not finish</div>
                        <div class="text-sm">${escapeHtml(message)}</div>
                        <ul class="list-disc list-inside text-sm opacity-80 mt-1">
                            <li>Check the pattern was printed at 100% scale</li>
                            <li>Check the camera is in focus and the light is even</li>
                            <li>Keep the whole board in the frame and hold it still</li>
                        </ul>
                    </div>
                </div>`;
        } else {
            result.replaceChildren();
            toast('Lens calibration stopped');
        }
        await calibration.refresh();
        this.showSetup();
    },

    async stopCalibration() {
        const camera = this.camera;
        const ok = await confirmDialog({
            title: 'Stop lens calibration?',
            body: 'The images captured so far are discarded.',
            confirmLabel: 'Stop',
            danger: true,
        });
        if (!ok || !camera) return;
        try {
            await api('/api/calibration/stop', { method: 'POST', body: { camera, kind: 'distortion' } });
            document.getElementById('distortion-status').textContent = 'Stopping';
        } catch (error) {
            toast(error.message, 'error');
        }
    },

    _stopPolling() {
        clearInterval(this.pollInterval);
        this.pollInterval = null;
    },

    _showOnLoad(imgId, placeholderId) {
        const img = document.getElementById(imgId);
        img.addEventListener('load', () => {
            img.hidden = false;
            document.getElementById(placeholderId).hidden = true;
        });
    },

    _resetView(imgId, placeholderId, text) {
        const img = document.getElementById(imgId);
        if (img.src.startsWith('blob:')) URL.revokeObjectURL(img.src);
        img.removeAttribute('src');
        img.hidden = true;
        const placeholder = document.getElementById(placeholderId);
        placeholder.textContent = text;
        placeholder.hidden = false;
    },

    _showFrame(imgId, data) {
        const img = document.getElementById(imgId);
        const oldSrc = img.src;
        img.src = URL.createObjectURL(new Blob([data], { type: 'image/jpeg' }));
        if (oldSrc.startsWith('blob:')) URL.revokeObjectURL(oldSrc);
    },

    // Resolves on the first frame, so the camera is open before the run starts. Rejects if the feed is closed first.
    _startFeed(camera) {
        this._stopFeed();
        this._resetView('distortion-feed', 'distortion-feed-placeholder', 'Connecting to camera');
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => this._stopFeed('The camera feed did not start. Check that the camera is connected.'), 10000);
            this.feedPending = { timeout, reject };
            const feed = openSocket('/ws/distortion-feed', (msg) => {
                if (this.feed !== feed) return;
                if (msg instanceof ArrayBuffer) {
                    if (this.feedPending) {
                        clearTimeout(timeout);
                        this.feedPending = null;
                        resolve();
                    }
                    this._showFrame('distortion-feed', msg);
                } else if (msg.error) {
                    this._stopFeed(msg.error);
                } else if (msg.type === 'metrics') {
                    this._updateFeedOverlay(msg);
                }
            }, {
                onOpen: () => feed.send({ camera }),
                onClose: () => { document.getElementById('distortion-feed-overlay').hidden = true; },
            });
            this.feed = feed;
        });
    },

    _updateFeedOverlay(metrics) {
        const el = document.getElementById('distortion-feed-overlay');
        el.hidden = false;
        el.classList.remove('text-success', 'text-warning', 'text-error');
        if (metrics.corners > 0) {
            el.classList.add(metrics.is_good ? 'text-success' : 'text-warning');
            el.textContent = `Corners: ${metrics.corners}  Blur: ${metrics.blur}`;
        } else {
            el.classList.add('text-error');
            el.textContent = 'No board detected';
        }
    },

    _stopFeed(error) {
        if (this.feedPending) {
            clearTimeout(this.feedPending.timeout);
            this.feedPending.reject(error ? new Error(error) : Object.assign(new Error('The camera feed was closed'), { cancelled: true }));
            this.feedPending = null;
        }
        if (this.feed) {
            this.feed.close();
            this.feed = null;
        }
        const img = document.getElementById('distortion-feed');
        if (img.src.startsWith('blob:')) URL.revokeObjectURL(img.src);
        img.removeAttribute('src');
        img.hidden = true;
        const placeholder = document.getElementById('distortion-feed-placeholder');
        placeholder.hidden = false;
        if (error) placeholder.textContent = error;
        document.getElementById('distortion-feed-overlay').hidden = true;
    },

    async printBoard() {
        try {
            const response = await fetch('/api/calibration/charuco-board');
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const url = URL.createObjectURL(await response.blob());

            const printWindow = window.open('', '_blank');
            printWindow.document.write(`<!DOCTYPE html>
                <html><head><title>ChArUco board, print at 100%</title>
                <style>
                    @page { size: A4; margin: 0; }
                    body { margin: 0; display: flex; justify-content: center; align-items: center; }
                    img { width: 100%; height: auto; }
                </style></head>
                <body><img src="${url}" onload="window.print()"></body></html>`);
            printWindow.document.close();
        } catch (error) {
            toast(`Could not make the pattern: ${error.message}`, 'error');
        }
    },

    toggleUndistortPreview() {
        if (this.preview) {
            this._stopPreview();
        } else {
            this._startPreview(this.selectedCamera());
        }
    },

    setPreviewMode(mode) {
        this.previewMode = mode;
        for (const m of ['side_by_side', 'raw', 'undistorted']) {
            document.getElementById('mode-' + m).classList.toggle('btn-active', m === mode);
        }
        if (this.preview) this.preview.send({ mode });
    },

    _startPreview(camera) {
        this._stopPreview();
        this._resetView('undistort-feed', 'undistort-placeholder', 'Connecting to camera');
        document.getElementById('undistort-preview-section').hidden = false;
        document.getElementById('undistort-preview-btn').lastChild.textContent = ' Hide preview';
        const preview = openSocket('/ws/undistort-preview', (msg) => {
            if (msg instanceof ArrayBuffer) {
                this._showFrame('undistort-feed', msg);
                return;
            }
            toast(msg.error || 'The lens preview stopped', 'error');
            this._stopPreview();
        }, {
            onOpen: () => {
                preview.send({ camera });
                if (this.previewMode !== 'side_by_side') preview.send({ mode: this.previewMode });
            },
        });
        this.preview = preview;
    },

    _stopPreview() {
        if (this.preview) {
            this.preview.close();
            this.preview = null;
        }
        this._resetView('undistort-feed', 'undistort-placeholder', 'Connecting to camera');
        document.getElementById('undistort-preview-section').hidden = true;
        document.getElementById('undistort-preview-btn').lastChild.textContent = ' Preview';
    },

    log(message) {
        const logContent = document.getElementById('distortion-log-content');
        const entry = document.createElement('div');
        entry.textContent = `[${new Date().toLocaleTimeString()}] ${message}`;
        logContent.appendChild(entry);
        logContent.scrollTop = logContent.scrollHeight;
    },
};

distortionCalibration.init();
