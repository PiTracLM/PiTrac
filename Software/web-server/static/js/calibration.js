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
        this.calibrationResults = {};  // Store results from calibration API

        this.init();
        this.setupPageCleanup();
    }

    async init() {
        await this.loadSystemStatus();

        await this.loadCalibrationData();

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
        const originalText = button?.textContent || 'Verify Ball Placement';

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

                if (result.image_url) {
                    const img = document.getElementById(`${camera}-image`);
                    img.src = result.image_url + '?t=' + Date.now(); // Cache bust
                    img.style.display = 'block';

                    const placeholder = img.parentElement.querySelector('.camera-placeholder');
                    if (placeholder) {
                        placeholder.style.display = 'none';
                    }
                }

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
}

const calibration = new CalibrationManager();
