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
        document.getElementById('distortion-status').textContent = 'Starting calibration...';
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
            const statusEl = document.getElementById('distortion-status');
            statusEl.textContent = 'Could not connect to camera';
            statusEl.style.color = '#f44336';
            document.getElementById('distortion-details').innerHTML = `
                <div style="margin-top: 0.5rem; color: var(--text-secondary, #aaa);">
                    <p>${this._escapeHtml(error.message || 'Check that the camera is connected and not in use by another program.')}</p>
                    <button class="btn btn-secondary" onclick="distortionCalibration.reset()" style="margin-top: 0.5rem;">
                        Back
                    </button>
                </div>
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
        }

        if (status.images_captured !== undefined) {
            const target = status.target_images || 40;
            const imagesOk = status.images_captured >= target;
            const reqImages = document.getElementById('req-images');
            if (reqImages) {
                reqImages.style.color = imagesOk ? '#4CAF50' : '#888';
                reqImages.innerHTML = `<span style="margin-right: 0.25rem;">${imagesOk ? '&#9745;' : '&#9744;'}</span> ${status.images_captured}/${target} images captured`;
            }
        }
        if (status.coverage && status.coverage.fraction !== undefined) {
            const coverageOk = status.coverage.fraction >= 1.0;
            const cellsCovered = Math.round(status.coverage.fraction * 9);
            const reqCoverage = document.getElementById('req-coverage');
            if (reqCoverage) {
                reqCoverage.style.color = coverageOk ? '#4CAF50' : '#888';
                reqCoverage.innerHTML = `<span style="margin-right: 0.25rem;">${coverageOk ? '&#9745;' : '&#9744;'}</span> ${cellsCovered}/9 areas covered`;
            }
        }
        if (status.tilt_fraction !== undefined) {
            const tiltOk = status.tilt_fraction >= 0.40;
            const reqTilt = document.getElementById('req-tilt');
            if (reqTilt) {
                reqTilt.style.color = tiltOk ? '#4CAF50' : '#888';
                reqTilt.innerHTML = `<span style="margin-right: 0.25rem;">${tiltOk ? '&#9745;' : '&#9744;'}</span> ${tiltOk ? 'Tilted angles used' : 'Need more tilted angles'}`;
            }
        }

        if (status.coverage && status.coverage.grid) {
            this._updateCoverageGrid(status.coverage);
        }
    },

    _initCoverageGrid() {
        const grid = document.getElementById('distortion-coverage-grid');
        grid.innerHTML = '';
        for (let i = 0; i < 9; i++) {
            const cell = document.createElement('div');
            cell.style.cssText =
                'background: #333; border-radius: 2px; transition: background 0.3s;';
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
                    if (count === 0) {
                        cells[idx].style.background = '#333';
                    } else if (count === 1) {
                        cells[idx].style.background = '#2d5a1e';
                    } else if (count === 2) {
                        cells[idx].style.background = '#3a7a25';
                    } else {
                        cells[idx].style.background = '#4CAF50';
                    }
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
            const response = await fetch('/api/calibration/data');
            const calibData = await response.json();

            const cameraData = this.currentCamera === 'camera1' ?
                calibData.camera1 : calibData.camera2;

            document.getElementById('distortion-progress').style.display = 'none';
            document.getElementById('distortion-results').style.display = 'block';

            const cameraLabel = this.currentCamera === 'camera1' ? 'Camera 1' : 'Camera 2';
            const rmsText = status.message || '';

            const resultsDiv = document.getElementById('distortion-results-data');
            resultsDiv.innerHTML = `
                <div style="padding: 1rem; background: var(--bg-tertiary, #252535); border-radius: 6px;">
                    <h3 style="margin-top: 0; color: #4CAF50;">${cameraLabel} -- Calibration Complete</h3>
                    <p style="color: var(--text-secondary, #aaa);">${rmsText}</p>
                    <p style="color: var(--text-secondary, #aaa); font-size: 0.9rem;">
                        Calibration data has been saved automatically.
                    </p>
                    <div style="background: #1a3a1a; padding: 0.75rem; border-radius: 4px; border: 1px solid #2a5a2a;">
                        <strong>What's next?</strong>
                        <ul style="margin: 0.5rem 0 0 0; padding-left: 1.2rem;">
                            <li>Use "Show Undistort Preview" below to visually verify straight lines look straight</li>
                            <li>This calibration is saved -- you only need to redo it if you change the lens</li>
                            <li>Proceed to ball-based calibration below when ready</li>
                        </ul>
                    </div>
                </div>
            `;

        } catch (error) {
            console.error('Error fetching calibration data:', error);
            this.log('Calibration completed but could not fetch results: ' + error.message);
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
        document.getElementById('distortion-status').textContent = 'Calibration Failed';
        document.getElementById('distortion-status').style.color = '#f44336';
        document.getElementById('distortion-details').innerHTML = `
            <div style="background: #3a1a1a; padding: 1rem; border-radius: 6px; border: 1px solid #5a2a2a; margin-top: 0.5rem;">
                <strong>Error:</strong> ${safeMessage}
                <br><br>
                <strong>Troubleshooting:</strong>
                <ul style="padding-left: 1.2rem; margin-bottom: 0.5rem;">
                    <li>Ensure the ChArUco board is printed at 100% scale</li>
                    <li>Check camera is focused properly</li>
                    <li>Ensure good, even lighting</li>
                    <li>Keep the entire board visible in the frame</li>
                    <li>Hold the board steady during capture</li>
                </ul>
                <button class="btn btn-secondary" onclick="distortionCalibration.reset()">
                    Try Again
                </button>
            </div>
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

        // Reset status text color
        const statusEl = document.getElementById('distortion-status');
        if (statusEl) statusEl.style.color = '';

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
                document.getElementById('distortion-feed-placeholder').textContent =
                    'Camera feed unavailable';
                document.getElementById('distortion-feed-placeholder').style.display = 'block';
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
        if (metrics.corners > 0) {
            el.style.display = 'block';
            el.style.color = metrics.is_good ? '#00ff00' : '#ff8800';
            el.textContent = `Corners: ${metrics.corners}  Blur: ${metrics.blur}`;
        } else {
            el.style.display = 'block';
            el.style.color = '#ff4444';
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
            btn.textContent = 'Show Undistort Preview';
        } else {
            section.style.display = 'block';
            btn.textContent = 'Hide Undistort Preview';
            this._startUndistortPreview();
        }
    },

    setPreviewMode(mode) {
        this.undistortMode = mode;
        for (const m of ['side_by_side', 'raw', 'undistorted']) {
            const el = document.getElementById('mode-' + m);
            if (el) el.style.fontWeight = m === mode ? 'bold' : 'normal';
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
                if (el && data[cam] && data[cam].calibration_matrix) {
                    el.textContent = 'Calibrated';
                    el.style.color = '#4CAF50';
                } else if (el) {
                    el.textContent = '';
                }
            }
        } catch (e) { /* ignore */ }
    }
};

document.addEventListener('DOMContentLoaded', () => {
    distortionCalibration.checkExistingCalibrations();
});
