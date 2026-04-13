const distortionCalibration = {
    currentCamera: null,
    pollInterval: null,
    feedSocket: null,
    undistortSocket: null,
    undistortMode: 'side_by_side',

    async startCalibration(camera) {
        this.currentCamera = camera;

        const cameraLabel = camera === 'camera1' ? 'Camera 1' : 'Camera 2';
        document.getElementById('distortion-camera-title').textContent =
            `Distortion Calibration - ${cameraLabel}`;

        document.getElementById('distortion-camera-selection').style.display = 'none';
        document.getElementById('distortion-progress').style.display = 'block';
        document.getElementById('distortion-results').style.display = 'none';

        document.getElementById('distortion-progress-bar').style.width = '0%';
        const statusEl = document.getElementById('distortion-status');
        statusEl.textContent = 'Starting calibration...';
        statusEl.classList.remove('text-error', 'text-success');
        document.getElementById('distortion-details').textContent = '';
        document.getElementById('distortion-log-content').innerHTML = '';

        this._initCoverageGrid();
        this.log('Starting distortion calibration for ' + cameraLabel);

        try {
            // Start the live feed FIRST so it owns the camera device,
            // then start calibration which reads from the shared frame buffer.
            await this._startFeed(camera);
            this.log('Camera feed connected');

            const response = await fetch(`/api/calibration/distortion/${camera}`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ target_images: 40 })
            });

            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }

            const result = await response.json();
            if (result.status === 'error') {
                throw new Error(result.message);
            }

            this.log('Calibration task started');
            this.startStatusPolling();

        } catch (error) {
            console.error('Error starting distortion calibration:', error);
            this._stopFeed();
            statusEl.textContent = 'Could not connect to camera';
            statusEl.classList.add('text-error');
            const safe = this._escapeHtml(error.message || 'Check that the camera is connected and not in use by another program.');
            document.getElementById('distortion-details').innerHTML = `
                <div class="alert alert-error mt-2">
                    <span>${safe}</span>
                </div>
                <button class="btn btn-ghost btn-sm mt-2" onclick="distortionCalibration.reset()">
                    Back
                </button>
            `;
        }
    },

    startStatusPolling() {
        this.pollInterval = setInterval(async () => {
            try {
                const response = await fetch('/api/calibration/status');
                const data = await response.json();

                if (!this.currentCamera) return;

                const status = data[this.currentCamera];
                if (!status) return;

                this._updateProgress(status);

                if (status.status === 'completed') {
                    this._handleSuccess(status);
                } else if (status.status === 'failed' || status.status === 'error') {
                    this._handleFailure(status.message || 'Calibration failed');
                }
            } catch (error) {
                console.error('Error polling status:', error);
            }
        }, 2000);
    },

    _updateProgress(status) {
        const progressBar = document.getElementById('distortion-progress-bar');
        const statusText = document.getElementById('distortion-status');
        const detailsText = document.getElementById('distortion-details');
        const pctLabel = document.getElementById('distortion-progress-pct');
        const hintEl = document.getElementById('distortion-hint');

        if (status.progress !== undefined) {
            progressBar.style.width = status.progress + '%';
            if (pctLabel) pctLabel.textContent = status.progress + '%';
        }

        if (status.message) {
            statusText.textContent = status.message;
        }

        if (status.hint && hintEl) {
            hintEl.textContent = status.hint;
        }

        if (status.images_captured !== undefined) {
            const target = status.target_images || 40;
            detailsText.textContent =
                `${status.images_captured} of ${target} good images captured`;

            const imagesOk = status.images_captured >= target;
            this._setRequirement('req-images', imagesOk,
                `${status.images_captured}/${target} images captured`);
        }
        if (status.coverage && status.coverage.fraction !== undefined) {
            const coverageOk = status.coverage.fraction >= 1.0;
            const cellsCovered = Math.round(status.coverage.fraction * 9);
            this._setRequirement('req-coverage', coverageOk,
                `${cellsCovered}/9 areas covered`);
        }
        if (status.tilt_fraction !== undefined) {
            const tiltOk = status.tilt_fraction >= 0.40;
            this._setRequirement('req-tilt', tiltOk,
                tiltOk ? 'Tilted angles used' : 'Need more tilted angles');
        }

        if (status.coverage && status.coverage.grid) {
            this._updateCoverageGrid(status.coverage);
        }
    },

    _setRequirement(id, satisfied, label) {
        const el = document.getElementById(id);
        if (!el) return;
        el.classList.toggle('text-success', satisfied);
        el.classList.toggle('opacity-60', !satisfied);
        const icon = satisfied ? 'check-square' : 'square';
        el.innerHTML = `<i data-lucide="${icon}" class="icon-sm"></i><span>${this._escapeHtml(label)}</span>`;
        if (window.lucide && window.lucide.createIcons) {
            window.lucide.createIcons({ nodes: [el] });
        }
    },

    _initCoverageGrid() {
        const grid = document.getElementById('distortion-coverage-grid');
        grid.innerHTML = '';
        for (let i = 0; i < 9; i++) {
            const cell = document.createElement('div');
            cell.className = 'coverage-cell';
            cell.dataset.count = '0';
            cell.dataset.index = i;
            grid.appendChild(cell);
        }
        document.getElementById('distortion-coverage-text').textContent = 'Coverage: 0%';
    },

    _updateCoverageGrid(coverage) {
        const grid = document.getElementById('distortion-coverage-grid');
        const cells = grid.children;

        for (let r = 0; r < 3; r++) {
            for (let c = 0; c < 3; c++) {
                const idx = r * 3 + c;
                // Mirror columns so grid matches the camera's perspective
                const mirroredCol = 2 - c;
                const count = coverage.grid[r][mirroredCol];
                if (cells[idx]) {
                    // Clamp to 0..3 for styling bucket
                    cells[idx].dataset.count = String(Math.min(count, 3));
                }
            }
        }

        const fraction = coverage.fraction || 0;
        const suggested = coverage.suggested_region || '';
        const cellsCovered = Math.round(fraction * 9);
        const coverageText = document.getElementById('distortion-coverage-text');
        coverageText.textContent = `${cellsCovered} of 9 areas covered`;
        if (fraction < 1.0 && suggested) {
            coverageText.textContent += ` \u2014 Try the ${suggested} area next`;
        }
    },

    async _handleSuccess(status) {
        this._stopPolling();
        this._stopFeed();
        this.log('Calibration completed successfully!');

        try {
            document.getElementById('distortion-progress').style.display = 'none';
            document.getElementById('distortion-results').style.display = 'block';

            const cameraLabel = this.currentCamera === 'camera1' ? 'Camera 1' : 'Camera 2';
            const rmsText = this._escapeHtml(status.message || '');

            const resultsDiv = document.getElementById('distortion-results-data');
            resultsDiv.innerHTML = `
                <div class="alert alert-success mb-3">
                    <i data-lucide="check-circle-2"></i>
                    <div>
                        <div class="font-semibold">${cameraLabel} &mdash; Calibration Complete</div>
                        <div class="text-sm opacity-80">${rmsText}</div>
                        <div class="text-sm opacity-60 mt-1">Calibration data has been saved automatically.</div>
                    </div>
                </div>
                <div class="bg-base-300 rounded-lg p-4 border border-base-300">
                    <div class="font-semibold mb-2">What's next?</div>
                    <ul class="list-disc list-inside space-y-1 text-sm opacity-80">
                        <li>Use "Show Undistort Preview" below to visually verify straight lines look straight</li>
                        <li>This calibration is saved &mdash; you only need to redo it if you change the lens</li>
                        <li>Switch to the Ball Calibration tab when ready</li>
                    </ul>
                </div>
            `;
            if (window.lucide && window.lucide.createIcons) {
                window.lucide.createIcons({ nodes: [resultsDiv] });
            }

        } catch (error) {
            console.error('Error rendering calibration results:', error);
            this.log('Calibration completed but could not render results: ' + error.message);
        }
    },

    _escapeHtml(str) {
        return String(str)
            .replace(/&/g, '&amp;')
            .replace(/</g, '&lt;')
            .replace(/>/g, '&gt;')
            .replace(/"/g, '&quot;');
    },

    _handleFailure(message) {
        this._stopPolling();
        this._stopFeed();
        this.log('Calibration failed: ' + message);

        const safeMessage = this._escapeHtml(message);
        const statusEl = document.getElementById('distortion-status');
        statusEl.textContent = 'Calibration Failed';
        statusEl.classList.remove('text-success');
        statusEl.classList.add('text-error');
        const details = document.getElementById('distortion-details');
        details.innerHTML = `
            <div class="alert alert-error mt-2">
                <div>
                    <div class="font-semibold">Error</div>
                    <div class="text-sm">${safeMessage}</div>
                </div>
            </div>
            <div class="bg-base-300 rounded-lg p-3 border border-base-300 mt-2 text-sm">
                <div class="font-semibold mb-1">Troubleshooting:</div>
                <ul class="list-disc list-inside space-y-0.5 opacity-80">
                    <li>Ensure the ChArUco board is printed at 100% scale</li>
                    <li>Check camera is focused properly</li>
                    <li>Ensure good, even lighting</li>
                    <li>Keep the entire board visible in the frame</li>
                    <li>Hold the board steady during capture</li>
                </ul>
            </div>
            <button class="btn btn-ghost btn-sm mt-2" onclick="distortionCalibration.reset()">
                Try Again
            </button>
        `;
    },

    async stopCalibration() {
        if (!confirm('Stop calibration? All progress for this session will be lost.')) {
            return;
        }
        try {
            await fetch('/api/calibration/stop', { method: 'POST' });
            this.log('Calibration stopped by user');
        } catch (error) {
            console.error('Error stopping calibration:', error);
        }
        this._stopPolling();
        this._stopFeed();
        this.reset();
    },

    reset() {
        this._stopPolling();
        this._stopFeed();
        this._stopUndistortPreview();
        this.currentCamera = null;

        document.getElementById('distortion-camera-selection').style.display = 'block';
        document.getElementById('distortion-progress').style.display = 'none';
        document.getElementById('distortion-results').style.display = 'none';

        const statusEl = document.getElementById('distortion-status');
        if (statusEl) statusEl.classList.remove('text-error', 'text-success');

        document.getElementById('distortion-log-content').innerHTML = '';
        this.checkExistingCalibrations();
    },

    _stopPolling() {
        if (this.pollInterval) {
            clearInterval(this.pollInterval);
            this.pollInterval = null;
        }
    },

    // Resolves on first frame, guaranteeing camera is open before calibration starts.
    _startFeed(camera) {
        this._stopFeed();
        return new Promise((resolve, reject) => {
            const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
            const ws = new WebSocket(`${proto}//${location.host}/ws/distortion-feed`);
            ws.binaryType = 'arraybuffer';
            let firstFrame = true;
            const timeout = setTimeout(() => {
                reject(new Error('Camera feed timed out -- check that the camera is connected'));
            }, 10000);

            ws.onopen = () => {
                ws.send(JSON.stringify({ camera }));
            };

            ws.onmessage = (event) => {
                if (typeof event.data === 'string') {
                    let msg;
                    try { msg = JSON.parse(event.data); }
                    catch (e) { console.warn('Bad JSON from feed:', e); return; }

                    if (msg.error) {
                        clearTimeout(timeout);
                        const ph = document.getElementById('distortion-feed-placeholder');
                        if (ph) {
                            ph.textContent = msg.error;
                            ph.style.display = 'block';
                        }
                        reject(new Error(msg.error));
                        return;
                    }
                    if (msg.type === 'metrics') {
                        this._updateFeedOverlay(msg);
                    }
                    return;
                }
                if (firstFrame) {
                    firstFrame = false;
                    clearTimeout(timeout);
                    document.getElementById('distortion-feed-placeholder').style.display = 'none';
                    resolve();
                }
                this._updateFeedImage('distortion-feed', event.data);
            };

            ws.onerror = () => {
                clearTimeout(timeout);
                const ph = document.getElementById('distortion-feed-placeholder');
                ph.textContent = 'Camera feed unavailable';
                ph.style.display = 'block';
                reject(new Error('Camera feed connection failed'));
            };

            ws.onclose = () => {
                this.feedSocket = null;
                const overlay = document.getElementById('distortion-feed-overlay');
                if (overlay) overlay.style.display = 'none';
            };

            this.feedSocket = ws;
        });
    },

    _updateFeedImage(elementId, data) {
        const blob = new Blob([data], { type: 'image/jpeg' });
        const url = URL.createObjectURL(blob);
        const img = document.getElementById(elementId);
        const oldSrc = img.src;
        img.src = url;
        if (oldSrc && oldSrc.startsWith('blob:')) URL.revokeObjectURL(oldSrc);
    },

    _updateFeedOverlay(metrics) {
        const el = document.getElementById('distortion-feed-overlay');
        if (!el) return;
        el.style.display = 'block';
        el.classList.remove('text-success', 'text-warning', 'text-error');
        if (metrics.corners > 0) {
            el.classList.add(metrics.is_good ? 'text-success' : 'text-warning');
            el.textContent = `Corners: ${metrics.corners}  Blur: ${metrics.blur}`;
        } else {
            el.classList.add('text-error');
            el.textContent = 'No board detected';
        }
    },

    _stopFeed() {
        if (this.feedSocket) {
            this.feedSocket.close();
            this.feedSocket = null;
        }
        const img = document.getElementById('distortion-feed');
        if (img && img.src && img.src.startsWith('blob:')) {
            URL.revokeObjectURL(img.src);
            img.src = '';
        }
        const overlay = document.getElementById('distortion-feed-overlay');
        if (overlay) overlay.style.display = 'none';
    },

    async printBoard() {
        try {
            const response = await fetch('/api/calibration/charuco-board');
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            const blob = await response.blob();
            const url = URL.createObjectURL(blob);

            const printWindow = window.open('', '_blank');
            printWindow.document.write(`<!DOCTYPE html>
                <html><head><title>ChArUco Board - Print at 100%</title>
                <style>
                    @page { size: A4; margin: 0; }
                    body { margin: 0; display: flex; justify-content: center; align-items: center; }
                    img { width: 100%; height: auto; }
                </style></head>
                <body><img src="${url}" onload="window.print()"></body></html>`);
            printWindow.document.close();
        } catch (error) {
            console.error('Error generating board:', error);
            alert('Failed to generate ChArUco board: ' + error.message);
        }
    },

    toggleUndistortPreview() {
        const section = document.getElementById('undistort-preview-section');
        const btn = document.getElementById('undistort-preview-btn');
        if (this.undistortSocket) {
            this._stopUndistortPreview();
            section.style.display = 'none';
            btn.innerHTML = '<i data-lucide="eye" class="icon-sm"></i> Show Undistort Preview';
        } else {
            section.style.display = 'block';
            btn.innerHTML = '<i data-lucide="eye-off" class="icon-sm"></i> Hide Undistort Preview';
            this._startUndistortPreview();
        }
        if (window.lucide && window.lucide.createIcons) {
            window.lucide.createIcons({ nodes: [btn] });
        }
    },

    setPreviewMode(mode) {
        this.undistortMode = mode;
        for (const m of ['side_by_side', 'raw', 'undistorted']) {
            const el = document.getElementById('mode-' + m);
            if (el) el.classList.toggle('btn-active', m === mode);
        }
        if (this.undistortSocket && this.undistortSocket.readyState === WebSocket.OPEN) {
            this.undistortSocket.send(JSON.stringify({ mode }));
        }
    },

    _startUndistortPreview() {
        this._stopUndistortPreview();
        const camera = this.currentCamera || 'camera1';
        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const ws = new WebSocket(`${proto}//${location.host}/ws/undistort-preview`);
        ws.binaryType = 'arraybuffer';

        ws.onopen = () => {
            ws.send(JSON.stringify({ camera }));
        };

        ws.onmessage = (event) => {
            if (typeof event.data === 'string') {
                try {
                    const msg = JSON.parse(event.data);
                    alert(msg.error || 'Undistort preview error');
                } catch (_) {
                    alert('Undistort preview error');
                }
                return;
            }
            this._updateFeedImage('undistort-feed', event.data);
        };

        ws.onerror = () => {
            alert('Could not start undistort preview. Make sure calibration data exists.');
        };

        ws.onclose = () => {
            this.undistortSocket = null;
        };

        this.undistortSocket = ws;
    },

    _stopUndistortPreview() {
        if (this.undistortSocket) {
            this.undistortSocket.close();
            this.undistortSocket = null;
        }
        const img = document.getElementById('undistort-feed');
        if (img && img.src && img.src.startsWith('blob:')) {
            URL.revokeObjectURL(img.src);
            img.src = '';
        }
    },

    log(message) {
        const logContent = document.getElementById('distortion-log-content');
        if (!logContent) return;

        const timestamp = new Date().toLocaleTimeString();
        const entry = document.createElement('div');
        entry.textContent = `[${timestamp}] ${message}`;
        logContent.appendChild(entry);
        logContent.scrollTop = logContent.scrollHeight;
    },

    async checkExistingCalibrations() {
        try {
            const resp = await fetch('/api/calibration/data');
            const data = await resp.json();
            for (const cam of ['camera1', 'camera2']) {
                const el = document.getElementById(cam + '-cal-status');
                if (!el) continue;
                if (data[cam] && data[cam].calibration_matrix) {
                    el.textContent = 'Calibrated';
                    el.classList.add('text-success');
                    el.classList.remove('opacity-70');
                } else {
                    el.textContent = '';
                    el.classList.remove('text-success');
                    el.classList.add('opacity-70');
                }
            }
        } catch (e) { /* ignore */ }
    }
};

document.addEventListener('DOMContentLoaded', () => {
    distortionCalibration.checkExistingCalibrations();
});
