/**
 * PiTrac Calibration UI Controller
 */

class CalibrationManager {
    constructor() {
        this.currentStep = 1;
        this.selectedCameras = [];
        this.calibrationMethod = null;
        this.calibrationInProgress = false;
        this.statusPollInterval = null;
        this.cameraPollIntervals = new Map();
        this.ballVerified = {
            camera1: false,
            camera2: false
        };
        this.calibrationResults = {};
        this.strobePollingTimer = null;

        this.init();
        this.setupPageCleanup();
    }

    async init() {
        await this.loadSystemStatus();

        await this.loadCalibrationData();

        await this.loadStrobeSettings();

        this.setupEventListeners();

        this.startStatusPolling();
    }

    /**
     * Setup cleanup handlers for page unload
     */
    setupPageCleanup() {
        window.addEventListener('beforeunload', () => {
            this.cleanup();
        });

        window.addEventListener('pagehide', () => {
            this.cleanup();
        });
    }

    /**
     * Cleanup all intervals and resources
     */
    cleanup() {
        if (this.statusPollInterval) {
            clearInterval(this.statusPollInterval);
            this.statusPollInterval = null;
        }

        if (this.strobePollingTimer) {
            clearTimeout(this.strobePollingTimer);
            this.strobePollingTimer = null;
        }

        this.cameraPollIntervals.forEach((intervalId) => {
            clearInterval(intervalId);
        });
        this.cameraPollIntervals.clear();
    }

    setupEventListeners() {
        document.querySelectorAll('input[name="camera"]').forEach(input => {
            input.addEventListener('change', (e) => {
                this.updateSelectedCameras(e.target.value);
            });
        });

        this.updateSelectedCameras('camera1');
    }

    updateSelectedCameras(value) {
        if (value === 'both') {
            this.selectedCameras = ['camera1', 'camera2'];
            document.getElementById('camera1-view').style.display = 'block';
            document.getElementById('camera2-view').style.display = 'block';
        } else {
            this.selectedCameras = [value];
            document.getElementById('camera1-view').style.display =
                value === 'camera1' ? 'block' : 'none';
            document.getElementById('camera2-view').style.display =
                value === 'camera2' ? 'block' : 'none';
        }
    }

    async loadSystemStatus() {
        try {
            const configResponse = await fetch('/api/config');
            if (configResponse.ok) {
                const config = await configResponse.json();
                const systemMode = config.system?.mode || 'single';
                document.getElementById('system-mode').textContent =
                    systemMode === 'single' ? 'Single Pi' : 'Dual Pi';
            }

            const statusResponse = await fetch('/api/pitrac/status');
            if (statusResponse.ok) {
                const status = await statusResponse.json();
                const statusElement = document.getElementById('pitrac-status');
                if (status.running) {
                    statusElement.textContent = 'Running';
                    statusElement.style.color = '#4CAF50';
                } else {
                    statusElement.textContent = 'Stopped';
                    statusElement.style.color = '#f44336';
                }
            }
        } catch (error) {
            console.error('Error loading system status:', error);
        }
    }

    async loadCalibrationData() {
        try {
            const response = await fetch('/api/calibration/data');
            if (response.ok) {
                const data = await response.json();
                this.displayCurrentCalibration(data);
            }
        } catch (error) {
            console.error('Error loading calibration data:', error);
        }
    }

    displayCurrentCalibration(data) {
        const container = document.getElementById('current-calibration-data');
        container.innerHTML = '';

        if (data.camera1) {
            this.addCalibrationDataItem(container, 'Camera 1 Focal Length',
                data.camera1.focal_length?.toFixed(3) || 'Not set');

            if (data.camera1.angles && Array.isArray(data.camera1.angles)) {
                this.addCalibrationDataItem(container, 'Camera 1 Angles',
                    `[${data.camera1.angles.map(a => parseFloat(a).toFixed(2)).join(', ')}]`);
            } else {
                this.addCalibrationDataItem(container, 'Camera 1 Angles', 'Not set');
            }
        }

        if (data.camera2) {
            this.addCalibrationDataItem(container, 'Camera 2 Focal Length',
                data.camera2.focal_length?.toFixed(3) || 'Not set');

            if (data.camera2.angles && Array.isArray(data.camera2.angles)) {
                this.addCalibrationDataItem(container, 'Camera 2 Angles',
                    `[${data.camera2.angles.map(a => parseFloat(a).toFixed(2)).join(', ')}]`);
            } else {
                this.addCalibrationDataItem(container, 'Camera 2 Angles', 'Not set');
            }
        }
    }

    addCalibrationDataItem(container, label, value) {
        const item = document.createElement('div');
        item.className = 'calibration-data-item';
        item.innerHTML = `
            <span class="calibration-data-label">${label}:</span>
            <span class="calibration-data-value">${value}</span>
        `;
        container.appendChild(item);
    }

    nextStep() {
        if (this.currentStep === 1) {
            this.showStep(2);
        } else if (this.currentStep === 2) {
            const allVerified = this.selectedCameras.every(cam => this.ballVerified[cam]);
            if (!allVerified) {
                this.showMessage('Please verify ball placement for all selected cameras', 'error');
                return;
            }
            this.showStep(3);
        } else if (this.currentStep === 3) {
        }
    }

    prevStep() {
        if (this.currentStep > 1) {
            this.showStep(this.currentStep - 1);
        }
    }

    showStep(stepNumber) {
        document.querySelectorAll('.wizard-content').forEach(content => {
            content.style.display = 'none';
        });

        document.getElementById(`step${stepNumber}`).style.display = 'block';

        document.querySelectorAll('.step').forEach(step => {
            const stepNum = parseInt(step.dataset.step);
            if (stepNum < stepNumber) {
                step.classList.add('completed');
                step.classList.remove('active');
            } else if (stepNum === stepNumber) {
                step.classList.add('active');
                step.classList.remove('completed');
            } else {
                step.classList.remove('active', 'completed');
            }
        });

        this.currentStep = stepNumber;
    }

    /**
     * Run auto calibration for the specified camera
     * @param {string} camera - Camera identifier (camera1 or camera2)
     * @param {Event} event - The click event from the button (optional)
     */
    async runAutoCalibration(camera, event) {
        if (!this.validateCameraName(camera)) {
            this.showMessage(`Invalid camera name: ${camera}`, 'error');
            return;
        }

        const button = event?.target || event?.currentTarget;
        const originalText = button?.textContent || 'Calibrate';

        try {
            if (button) {
                button.disabled = true;
                button.textContent = 'Calibrating...';
            }

            const response = await fetch(`/api/calibration/auto/${camera}`, {
                method: 'POST'
            });

            if (response.ok) {
                const result = await response.json();
                if (result.status === 'success') {
                    // Display the calibration image if available
                    if (result.calibration_data && result.calibration_data.image_path) {
                        const img = document.getElementById(`${camera}-image`);
                        // Convert the full path to a web-accessible URL
                        const imageName = result.calibration_data.image_path.split('/').pop();
                        img.src = `/api/images/${imageName}`;
                        img.style.display = 'block';

                        const placeholder = img.parentElement.querySelector('.camera-placeholder');
                        if (placeholder) {
                            placeholder.style.display = 'none';
                        }
                    }

                    this.showMessage(`Calibration successful for ${camera}`, 'success');
                } else {
                    this.showMessage(`Calibration failed: ${result.message}`, 'error');
                }
            } else {
                this.showMessage('Calibration request failed', 'error');
            }
        } catch (error) {
            console.error('Error running calibration:', error);
            this.showMessage('Error running calibration', 'error');
        } finally {
            if (button) {
                button.disabled = false;
                button.textContent = originalText;
            }
        }
    }

    /**
     * Check ball location in the camera view
     * @param {string} camera - Camera identifier (camera1 or camera2)
     * @param {Event} event - The click event from the button (optional)
     */
    async checkBallLocation(camera, event) {
        if (!this.validateCameraName(camera)) {
            this.showMessage(`Invalid camera name: ${camera}`, 'error');
            return;
        }

        const button = event?.target || event?.currentTarget;
        const originalText = button?.textContent || 'Check Ball Location';

        try {
            if (button) {
                button.disabled = true;
                button.textContent = 'Checking...';
            }

            const response = await fetch(`/api/calibration/ball-location/${camera}`, {
                method: 'POST'
            });

            if (response.ok) {
                const result = await response.json();
                const statusDiv = document.getElementById(`${camera}-ball-status`);

                if (result.ball_found) {
                    statusDiv.className = 'ball-status success';
                    statusDiv.textContent = `Ball detected at position (${result.ball_info?.x || 0}, ${result.ball_info?.y || 0})`;
                    this.ballVerified[camera] = true;

                    const allVerified = this.selectedCameras.every(cam => this.ballVerified[cam]);
                    if (allVerified) {
                        document.getElementById('verify-next').disabled = false;
                        document.getElementById('verification-message').className = 'alert alert-success';
                        document.getElementById('verification-message').textContent =
                            '✅ Ball placement verified! Ready to proceed with calibration.';
                    }
                } else {
                    statusDiv.className = 'ball-status error';
                    statusDiv.textContent = 'Ball not detected - please adjust placement';
                    this.ballVerified[camera] = false;
                }
            } else {
                this.showMessage('Failed to check ball location', 'error');
            }
        } catch (error) {
            console.error('Error checking ball location:', error);
            this.showMessage('Error checking ball location', 'error');
        } finally {
            if (button) {
                button.disabled = false;
                button.textContent = originalText;
            }
        }
    }

    selectMethod(method) {
        this.calibrationMethod = method;

        document.querySelector('.calibration-options').style.display = 'none';

        document.getElementById('calibration-progress').style.display = 'block';

        this.startCalibration(method);
    }

    async startCalibration(method) {
        this.calibrationInProgress = true;

        document.getElementById('calibration-log-content').innerHTML = '';
        this.addLogEntry('Starting calibration process...');

        for (const camera of this.selectedCameras) {
            await this.calibrateCamera(camera, method);
        }
    }

    async calibrateCamera(camera, method) {
        try {
            this.addLogEntry(`Starting ${method} calibration for ${camera}...`);

            const progressBar = document.getElementById(`${camera}-progress`);
            const statusText = document.getElementById(`${camera}-status`);
            const detailsDiv = document.getElementById(`${camera}-details`);

            progressBar.style.width = '10%';
            statusText.textContent = 'Initializing...';
            detailsDiv.innerHTML = '';

            const endpoint = method === 'auto'
                ? `/api/calibration/auto/${camera}`
                : `/api/calibration/manual/${camera}`;

            const response = await fetch(endpoint, {
                method: 'POST'
            });

            if (response.ok) {
                const result = await response.json();

                progressBar.style.width = '50%';
                statusText.textContent = 'Calibrating...';

                const finalResult = await this.pollForCompletion(camera);

                if (finalResult && finalResult.status === 'success') {
                    this.addLogEntry(`${camera} calibration completed successfully`);
                    this.addLogEntry(`  Completion method: ${finalResult.completion_method || 'unknown'}`);

                    const details = [];
                    if (finalResult.api_success) {
                        details.push('API Callbacks Received');
                    }
                    if (finalResult.focal_length_received) {
                        details.push('Focal Length');
                    }
                    if (finalResult.angles_received) {
                        details.push('Camera Angles');
                    }

                    detailsDiv.innerHTML = `<small>${details.join(' | ')}</small>`;

                    progressBar.style.width = '100%';
                    statusText.textContent = 'Completed';

                    if (!this.calibrationResults) {
                        this.calibrationResults = {};
                    }
                    this.calibrationResults[camera] = finalResult;

                    const allDone = this.selectedCameras.every(cam => {
                        const status = document.getElementById(`${cam}-status`).textContent;
                        return status === 'Completed' || status === 'Failed';
                    });

                    if (allDone) {
                        await this.showCalibrationResults();
                    }
                } else {
                    const message = result.message || finalResult?.message || 'Unknown error';
                    this.addLogEntry(`${camera} calibration failed: ${message}`);

                    const details = [];
                    if (finalResult?.completion_method) {
                        details.push(`Method: ${finalResult.completion_method}`);
                    }
                    if (finalResult?.focal_length_received) {
                        details.push('Focal length received');
                    }
                    if (finalResult?.angles_received) {
                        details.push('Angles received');
                    }

                    if (details.length > 0) {
                        detailsDiv.innerHTML = `<small style="color: #ff9800;">${details.join(' | ')}</small>`;
                    }

                    progressBar.style.width = '100%';
                    progressBar.style.background = '#f44336';
                    statusText.textContent = 'Failed';
                }
            } else {
                throw new Error('Failed to start calibration');
            }
        } catch (error) {
            console.error(`Error calibrating ${camera}:`, error);
            this.addLogEntry(`Error calibrating ${camera}: ${error.message}`);

            const progressBar = document.getElementById(`${camera}-progress`);
            const statusText = document.getElementById(`${camera}-status`);
            progressBar.style.width = '100%';
            progressBar.style.background = '#f44336';
            statusText.textContent = 'Error';
        }
    }

    async pollForCompletion(camera, timeout = 180000) {
        const startTime = Date.now();

        while (Date.now() - startTime < timeout) {
            const response = await fetch('/api/calibration/status');
            if (response.ok) {
                const status = await response.json();
                const cameraStatus = status[camera];

                const progressBar = document.getElementById(`${camera}-progress`);
                const statusText = document.getElementById(`${camera}-status`);

                if (cameraStatus && cameraStatus.progress) {
                    progressBar.style.width = `${cameraStatus.progress}%`;
                }

                if (cameraStatus && cameraStatus.message) {
                    statusText.textContent = cameraStatus.message;
                }

                if (cameraStatus &&
                    (cameraStatus.status === 'completed' ||
                     cameraStatus.status === 'failed' ||
                     cameraStatus.status === 'error')) {
                    return cameraStatus;
                }
            }

            await new Promise(resolve => setTimeout(resolve, 1000));
        }

        throw new Error('Calibration timeout');
    }

    async showCalibrationResults() {
        const response = await fetch('/api/calibration/data');
        if (response.ok) {
            const data = await response.json();

            if (this.selectedCameras.includes('camera1')) {
                const result = this.calibrationResults?.camera1;
                const card = document.getElementById('camera1-result-card');
                card.style.display = 'block';

                document.getElementById('camera1-result-status').textContent =
                    result?.status === 'success' ? 'Success' : 'Failed';

                document.getElementById('camera1-completion-method').textContent =
                    result?.completion_method
                        ? `${result.completion_method} ${result.api_success ? '(API callbacks ✓)' : ''}`
                        : '--';

                if (data.camera1) {
                    document.getElementById('camera1-focal').textContent =
                        data.camera1.focal_length?.toFixed(3) || '--';

                    if (data.camera1.angles && Array.isArray(data.camera1.angles)) {
                        document.getElementById('camera1-angles').textContent =
                            `[${data.camera1.angles.map(a => parseFloat(a).toFixed(2)).join(', ')}]`;
                    } else {
                        document.getElementById('camera1-angles').textContent = '--';
                    }
                }
            } else {
                document.getElementById('camera1-result-card').style.display = 'none';
            }

            if (this.selectedCameras.includes('camera2')) {
                const result = this.calibrationResults?.camera2;
                const card = document.getElementById('camera2-result-card');
                card.style.display = 'block';

                document.getElementById('camera2-result-status').textContent =
                    result?.status === 'success' ? 'Success' : 'Failed';

                document.getElementById('camera2-completion-method').textContent =
                    result?.completion_method
                        ? `${result.completion_method} ${result.api_success ? '(API callbacks ✓)' : ''}`
                        : '--';

                if (data.camera2) {
                    document.getElementById('camera2-focal').textContent =
                        data.camera2.focal_length?.toFixed(3) || '--';

                    if (data.camera2.angles && Array.isArray(data.camera2.angles)) {
                        document.getElementById('camera2-angles').textContent =
                            `[${data.camera2.angles.map(a => parseFloat(a).toFixed(2)).join(', ')}]`;
                    } else {
                        document.getElementById('camera2-angles').textContent = '--';
                    }
                }
            } else {
                document.getElementById('camera2-result-card').style.display = 'none';
            }

            this.displayCurrentCalibration(data);
        }

        this.showStep(4);
        this.calibrationInProgress = false;
    }

    async stopCalibration() {
        if (confirm('Are you sure you want to stop the calibration process?')) {
            try {
                const response = await fetch('/api/calibration/stop', {
                    method: 'POST'
                });

                if (response.ok) {
                    this.addLogEntry('Calibration stopped by user');
                    this.calibrationInProgress = false;

                    this.selectedCameras.forEach(camera => {
                        if (this[`${camera}PollInterval`]) {
                            clearInterval(this[`${camera}PollInterval`]);
                        }
                    });

                    this.restart();
                }
            } catch (error) {
                console.error('Error stopping calibration:', error);
            }
        }
    }

    restart() {
        this.currentStep = 1;
        this.calibrationMethod = null;
        this.calibrationInProgress = false;
        this.ballVerified = {
            camera1: false,
            camera2: false
        };

        this.showStep(1);
        document.querySelector('.calibration-options').style.display = 'block';
        document.getElementById('calibration-progress').style.display = 'none';
        document.getElementById('verify-next').disabled = true;

        document.querySelectorAll('.camera-preview img').forEach(img => {
            img.style.display = 'none';
        });
        document.querySelectorAll('.camera-placeholder').forEach(placeholder => {
            placeholder.style.display = 'flex';
        });

        document.querySelectorAll('.ball-status').forEach(status => {
            status.textContent = '';
            status.className = 'ball-status';
        });

        document.querySelectorAll('.progress-fill').forEach(bar => {
            bar.style.width = '0%';
            bar.style.background = '';
        });
    }

    addLogEntry(message) {
        const logContent = document.getElementById('calibration-log-content');
        const timestamp = new Date().toLocaleTimeString();
        const entry = document.createElement('div');
        entry.textContent = `[${timestamp}] ${message}`;
        logContent.appendChild(entry);
        logContent.scrollTop = logContent.scrollHeight;
    }

    /**
     * Validate camera name is valid
     * @param {string} camera - Camera identifier to validate
     * @returns {boolean} True if valid camera name
     */
    validateCameraName(camera) {
        const validCameras = ['camera1', 'camera2'];
        return validCameras.includes(camera);
    }

    showMessage(message, type = 'info') {
        const messageDiv = document.getElementById('verification-message');
        if (messageDiv) {
            messageDiv.className = `alert alert-${type}`;
            messageDiv.textContent = message;
        }
    }

    startStatusPolling() {
        this.statusPollInterval = setInterval(() => {
            if (!this.calibrationInProgress) {
                this.loadSystemStatus();
            }
        }, 5000);
    }

    // -- Strobe Calibration --

    async loadStrobeSettings() {
        try {
            const [settingsRes, configRes] = await Promise.all([
                fetch('/api/strobe-calibration/settings'),
                fetch('/api/config?key=gs_config.strobing.kConnectionBoardVersion')
            ]);

            const calBtn = document.getElementById('strobe-calibrate-btn');
            const diagBtn = document.getElementById('strobe-diagnostics-btn');
            const controls = document.getElementById('strobe-controls');
            const warning = document.getElementById('strobe-board-warning');

            let boardVersion = null;
            if (configRes.ok) {
                const config = await configRes.json();
                boardVersion = config.data;
            }

            const boardEl = document.getElementById('strobe-board-version');
            boardEl.textContent = boardVersion ? 'V' + boardVersion : 'Not set';

            const isV3 = boardVersion !== null && parseInt(boardVersion) === 3;
            if (!isV3) {
                calBtn.disabled = true;
                diagBtn.disabled = true;
                controls.style.opacity = '0.5';
                warning.style.display = 'block';
            } else {
                calBtn.disabled = false;
                diagBtn.disabled = false;
                controls.style.opacity = '1';
                warning.style.display = 'none';
            }

            if (settingsRes.ok) {
                const settings = await settingsRes.json();
                const dacEl = document.getElementById('strobe-saved-dac');
                if (settings.dac_setting !== null && settings.dac_setting !== undefined) {
                    dacEl.textContent = '0x' + parseInt(settings.dac_setting).toString(16).toUpperCase().padStart(2, '0');
                    if (isV3) calBtn.textContent = 'Recalibrate';
                } else {
                    dacEl.textContent = 'Not calibrated';
                    if (isV3) calBtn.textContent = 'Calibrate';
                }
            }
        } catch (error) {
            console.error('Error loading strobe settings:', error);
        }
    }

    async startStrobeCalibration() {
        const btn = document.getElementById('strobe-calibrate-btn');
        const cancelBtn = document.getElementById('strobe-cancel-btn');
        const originalText = btn.textContent;

        const ledType = document.getElementById('strobe-led-type').value;
        const targetCurrent = ledType === 'v3' ? 10.0 : 9.0;

        try {
            btn.disabled = true;
            btn.textContent = 'Starting...';
            cancelBtn.style.display = '';

            // Reset and show progress area, hide stale results
            document.getElementById('strobe-progress-area').style.display = 'block';
            document.getElementById('strobe-result-area').style.display = 'none';
            document.getElementById('strobe-progress-fill').style.width = '0%';
            document.getElementById('strobe-progress-fill').style.background = '';
            document.getElementById('strobe-progress-message').textContent = 'Starting...';
            document.getElementById('strobe-state').textContent = 'Running';

            const response = await fetch('/api/strobe-calibration/start', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    led_type: ledType,
                    target_current: targetCurrent,
                    overwrite: true
                })
            });

            if (!response.ok) {
                const err = await response.json().catch(() => ({}));
                throw new Error(err.error || 'Failed to start calibration');
            }

            btn.textContent = 'Calibrating...';
            this.pollStrobeStatus();
        } catch (error) {
            console.error('Error starting strobe calibration:', error);
            btn.disabled = false;
            btn.textContent = originalText;
            cancelBtn.style.display = 'none';
            document.getElementById('strobe-state').textContent = 'Failed';
            document.getElementById('strobe-progress-message').textContent = error.message;
        }
    }

    pollStrobeStatus() {
        this.strobePollingTimer = setTimeout(async () => {
            try {
                const response = await fetch('/api/strobe-calibration/status');
                if (!response.ok) {
                    this.pollStrobeStatus();
                    return;
                }

                const status = await response.json();
                const progressFill = document.getElementById('strobe-progress-fill');
                const progressMsg = document.getElementById('strobe-progress-message');

                if (status.progress !== undefined) {
                    progressFill.style.width = status.progress + '%';
                }
                if (status.message) {
                    progressMsg.textContent = status.message;
                }

                if (status.state === 'complete' || status.state === 'completed') {
                    this.onStrobeCalibrationDone(status);
                } else if (status.state === 'failed' || status.state === 'error') {
                    this.onStrobeCalibrationFailed(status);
                } else if (status.state === 'cancelled') {
                    this.onStrobeCalibrationCancelled();
                } else {
                    this.pollStrobeStatus();
                }
            } catch (error) {
                console.error('Error polling strobe status:', error);
                this.pollStrobeStatus();
            }
        }, 500);
    }

    onStrobeCalibrationDone(status) {
        const btn = document.getElementById('strobe-calibrate-btn');
        const cancelBtn = document.getElementById('strobe-cancel-btn');
        btn.disabled = false;
        btn.textContent = 'Recalibrate';
        cancelBtn.style.display = 'none';

        document.getElementById('strobe-progress-fill').style.width = '100%';
        document.getElementById('strobe-state').textContent = 'Complete';

        // Show results
        const resultArea = document.getElementById('strobe-result-area');
        const resultCard = document.getElementById('strobe-result-card');
        resultCard.style.borderColor = 'var(--success)';
        document.getElementById('strobe-result-title').textContent = 'Calibration Successful';

        if (status.dac_setting !== undefined) {
            document.getElementById('strobe-result-dac').textContent =
                '0x' + status.dac_setting.toString(16).toUpperCase().padStart(2, '0');
        }
        if (status.led_current !== undefined) {
            document.getElementById('strobe-result-current').textContent = status.led_current.toFixed(2) + ' A';
        }
        if (status.ldo_voltage !== undefined) {
            document.getElementById('strobe-result-ldo').textContent = status.ldo_voltage.toFixed(2) + ' V';
        }

        resultArea.style.display = 'block';

        // Refresh the saved DAC display
        this.loadStrobeSettings();
    }

    onStrobeCalibrationFailed(status) {
        const cancelBtn = document.getElementById('strobe-cancel-btn');
        cancelBtn.style.display = 'none';
        this.loadStrobeSettings();

        document.getElementById('strobe-progress-fill').style.width = '100%';
        document.getElementById('strobe-progress-fill').style.background = 'var(--error)';
        document.getElementById('strobe-state').textContent = 'Failed';
        document.getElementById('strobe-progress-message').textContent =
            status.message || 'Calibration failed';

        // Show failure in result area
        const resultArea = document.getElementById('strobe-result-area');
        const resultCard = document.getElementById('strobe-result-card');
        resultCard.style.borderColor = 'var(--error)';
        document.getElementById('strobe-result-title').textContent = 'Calibration Failed';
        document.getElementById('strobe-result-dac').textContent = '--';
        document.getElementById('strobe-result-current').textContent = '--';
        document.getElementById('strobe-result-ldo').textContent = '--';
        resultArea.style.display = 'block';
    }

    onStrobeCalibrationCancelled() {
        const cancelBtn = document.getElementById('strobe-cancel-btn');
        cancelBtn.style.display = 'none';

        document.getElementById('strobe-state').textContent = 'Idle';
        document.getElementById('strobe-progress-message').textContent = 'Cancelled by user';
        this.loadStrobeSettings();
    }

    async cancelStrobeCalibration() {
        try {
            const cancelBtn = document.getElementById('strobe-cancel-btn');
            cancelBtn.disabled = true;
            cancelBtn.textContent = 'Cancelling...';

            await fetch('/api/strobe-calibration/cancel', { method: 'POST' });
            // Polling loop will pick up the cancelled state
        } catch (error) {
            console.error('Error cancelling strobe calibration:', error);
        }
    }

    async readStrobeDiagnostics() {
        const btn = document.getElementById('strobe-diagnostics-btn');
        const originalText = btn.textContent;

        try {
            btn.disabled = true;
            btn.textContent = 'Reading...';

            const response = await fetch('/api/strobe-calibration/diagnostics');
            if (!response.ok) {
                throw new Error('Failed to read diagnostics');
            }

            const data = await response.json();
            const grid = document.getElementById('strobe-diagnostics-grid');
            grid.innerHTML = '';

            const items = [
                { label: 'LDO Voltage', value: data.ldo_voltage != null ? data.ldo_voltage.toFixed(2) + ' V' : '--' },
                { label: 'LED Current', value: data.led_current != null ? data.led_current.toFixed(2) + ' A' : '--' },
                { label: 'ADC CH0 Raw', value: data.adc_ch0_raw != null ? data.adc_ch0_raw : '--' },
                { label: 'ADC CH1 Raw', value: data.adc_ch1_raw != null ? data.adc_ch1_raw : '--' }
            ];

            items.forEach(item => {
                this.addCalibrationDataItem(grid, item.label, item.value);
            });

            // Handle warnings
            const warningEl = document.getElementById('strobe-diagnostics-warning');
            if (data.warning) {
                warningEl.textContent = data.warning;
                warningEl.style.display = 'block';
            } else {
                warningEl.style.display = 'none';
            }

            document.getElementById('strobe-diagnostics-area').style.display = 'block';
        } catch (error) {
            console.error('Error reading strobe diagnostics:', error);
        } finally {
            btn.disabled = false;
            btn.textContent = originalText;
        }
    }
}

const calibration = new CalibrationManager();
