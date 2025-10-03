/* globals setTheme, closeModal */

let runningTools = new Set();
let outputBuffer = [];
const MAX_OUTPUT_LINES = 1000;
let uploadedImageFilename = null;

document.addEventListener('DOMContentLoaded', () => {
    loadAvailableTools();
    startOutputPolling();
    setupImageUpload();
});

async function loadAvailableTools() {
    try {
        const response = await fetch('/api/testing/tools');
        const data = await response.json();
        
        Object.entries(data).forEach(([category, tools]) => {
            const container = document.getElementById(`${category}-tools`);
            if (container) {
                container.innerHTML = '';
                tools.forEach(tool => {
                    container.appendChild(createToolCard(tool));
                });
            }
        });
    } catch (error) {
        console.error('Failed to load testing tools:', error);
        showError('Failed to load testing tools');
    }
}

function createToolCard(tool) {
    const card = document.createElement('div');
    card.className = 'tool-card';
    card.dataset.toolId = tool.id;
    
    const isRunning = runningTools.has(tool.id);
    
    card.innerHTML = `
        <div class="tool-header">
            <h3 class="tool-name">${tool.name}</h3>
            ${tool.requires_sudo ? '<span class="sudo-badge">sudo</span>' : ''}
        </div>
        <p class="tool-description">${tool.description}</p>
        <div class="tool-actions">
            <button class="btn btn-primary run-btn" 
                    onclick="runTool('${tool.id}')"
                    ${isRunning ? 'disabled' : ''}>
                ${isRunning ? 'Running...' : 'Run Test'}
            </button>
            ${isRunning ? `<button class="btn btn-danger" onclick="stopTool('${tool.id}')">Stop</button>` : ''}
        </div>
    `;
    
    if (isRunning) {
        card.classList.add('running');
    }
    
    return card;
}

async function runTool(toolId) {
    const card = document.querySelector(`[data-tool-id="${toolId}"]`);
    const runBtn = card.querySelector('.run-btn');
    
    runBtn.disabled = true;
    runBtn.textContent = 'Starting...';
    
    try {
        const response = await fetch(`/api/testing/run/${toolId}`, {
            method: 'POST'
        });
        
        const result = await response.json();
        
        if (result.status === 'started') {
            runningTools.add(toolId);
            card.classList.add('running');
            runBtn.textContent = 'Running...';
            
            const actionsDiv = card.querySelector('.tool-actions');
            if (!actionsDiv.querySelector('.btn-danger')) {
                const stopBtn = document.createElement('button');
                stopBtn.className = 'btn btn-danger';
                stopBtn.textContent = 'Stop';
                stopBtn.onclick = () => stopTool(toolId);
                actionsDiv.appendChild(stopBtn);
            }
            
            appendOutput(`[${new Date().toLocaleTimeString()}] Started ${toolId}`, 'info');
        } else {
            handleToolResult(toolId, result);
        }
    } catch (error) {
        console.error(`Failed to run tool ${toolId}:`, error);
        showError(`Failed to run tool: ${error.message}`);
        runBtn.disabled = false;
        runBtn.textContent = 'Run Test';
    }
}

async function stopTool(toolId) {
    try {
        const response = await fetch(`/api/testing/stop/${toolId}`, {
            method: 'POST'
        });
        
        const result = await response.json();
        
        if (result.status === 'success') {
            runningTools.delete(toolId);
            updateToolCard(toolId);
            appendOutput(`[${new Date().toLocaleTimeString()}] Stopped ${toolId}`, 'warning');
        }
    } catch (error) {
        console.error(`Failed to stop tool ${toolId}:`, error);
        showError(`Failed to stop tool: ${error.message}`);
    }
}

function handleToolResult(toolId, result) {
    runningTools.delete(toolId);
    updateToolCard(toolId);
    
    const timestamp = new Date().toLocaleTimeString();
    
    if (result.status === 'success') {
        appendOutput(`[${timestamp}] ${toolId} completed successfully`, 'success');
        
        if (result.output) {
            appendOutput('--- Output ---', 'info');
            appendOutput(result.output);
        }
        
        if (result.image_path) {
            showImageResult(toolId, result.image_url);
        }
    } else if (result.status === 'failed') {
        appendOutput(`[${timestamp}] ${toolId} failed`, 'error');
        
        if (result.error) {
            appendOutput('--- Error ---', 'error');
            appendOutput(result.error);
        }
    } else if (result.status === 'timeout') {
        appendOutput(`[${timestamp}] ${toolId} timed out`, 'warning');
    }
}

function updateToolCard(toolId) {
    const card = document.querySelector(`[data-tool-id="${toolId}"]`);
    if (!card) return;
    
    const runBtn = card.querySelector('.run-btn');
    const stopBtn = card.querySelector('.btn-danger');
    
    card.classList.remove('running');
    runBtn.disabled = false;
    runBtn.textContent = 'Run Test';
    
    if (stopBtn) {
        stopBtn.remove();
    }
}

async function startOutputPolling() {
    setInterval(async () => {
        if (runningTools.size === 0) return;
        
        try {
            const response = await fetch('/api/testing/status');
            const data = await response.json();
            
            for (const toolId of runningTools) {
                if (data.results && data.results[toolId]) {
                    handleToolResult(toolId, data.results[toolId]);
                }
            }
            
            if (data.running) {
                const currentlyRunning = new Set(data.running);
                for (const toolId of runningTools) {
                    if (!currentlyRunning.has(toolId)) {
                        runningTools.delete(toolId);
                        updateToolCard(toolId);
                    }
                }
            }
        } catch (error) {
            console.error('Failed to poll status:', error);
        }
    }, 2000); // Poll every 2 seconds
}

function appendOutput(text, className = '') {
    const outputDiv = document.getElementById('testOutput');
    
    const placeholder = outputDiv.querySelector('.output-placeholder');
    if (placeholder) {
        placeholder.remove();
    }
    
    const lines = text.split('\n');
    
    lines.forEach(line => {
        if (!line.trim()) return;
        
        const lineDiv = document.createElement('div');
        lineDiv.className = `output-line ${className}`;
        lineDiv.textContent = line;
        
        outputBuffer.push(lineDiv);
        outputDiv.appendChild(lineDiv);
    });
    
    while (outputBuffer.length > MAX_OUTPUT_LINES) {
        const oldLine = outputBuffer.shift();
        oldLine.remove();
    }
    
    outputDiv.scrollTop = outputDiv.scrollHeight;
}

// eslint-disable-next-line no-unused-vars
function clearOutput() {
    const outputDiv = document.getElementById('testOutput');
    outputDiv.innerHTML = '<div class="output-placeholder">Select a test tool to see output here</div>';
    outputBuffer = [];
}

function showImageResult(toolId, imageUrl) {
    const modal = document.getElementById('testModal');
    const modalTitle = document.getElementById('modalTitle');
    const modalBody = document.getElementById('modalBody');
    
    modalTitle.textContent = `Image Result: ${toolId}`;
    modalBody.innerHTML = `
        <div class="image-result">
            <img src="${imageUrl}" alt="${toolId} result" style="max-width: 100%; height: auto;">
            <div class="image-actions">
                <a href="${imageUrl}" download class="btn btn-primary">Download</a>
            </div>
        </div>
    `;
    
    modal.style.display = 'block';
}

function showError(message) {
    appendOutput(`[ERROR] ${message}`, 'error');
}

// Image Upload Functions
function setupImageUpload() {
    const uploadArea = document.getElementById('uploadArea');
    const fileInput = document.getElementById('imageUpload');

    // Click to upload
    uploadArea.addEventListener('click', () => fileInput.click());

    // File input change
    fileInput.addEventListener('change', handleFileSelect);

    // Drag and drop
    uploadArea.addEventListener('dragover', (e) => {
        e.preventDefault();
        uploadArea.classList.add('drag-over');
    });

    uploadArea.addEventListener('dragleave', () => {
        uploadArea.classList.remove('drag-over');
    });

    uploadArea.addEventListener('drop', (e) => {
        e.preventDefault();
        uploadArea.classList.remove('drag-over');

        if (e.dataTransfer.files.length > 0) {
            handleFileSelect({ target: { files: e.dataTransfer.files } });
        }
    });
}

async function handleFileSelect(event) {
    const file = event.target.files[0];
    if (!file) return;

    // Validate file type
    if (!file.type.startsWith('image/')) {
        showError('Please select an image file');
        return;
    }

    // Show preview
    const reader = new FileReader();
    reader.onload = (e) => {
        document.getElementById('previewImg').src = e.target.result;
        document.getElementById('imageName').textContent = file.name;
        document.getElementById('uploadArea').style.display = 'none';
        document.getElementById('imagePreview').style.display = 'block';
        document.getElementById('runPipelineBtn').style.display = 'block';
    };
    reader.readAsDataURL(file);

    // Upload to server
    const formData = new FormData();
    formData.append('file', file);

    try {
        appendOutput('[INFO] Uploading image...', 'info');

        const response = await fetch('/api/testing/upload-image', {
            method: 'POST',
            body: formData
        });

        const result = await response.json();

        if (result.status === 'success') {
            uploadedImageFilename = result.filename;
            appendOutput(`[SUCCESS] Image uploaded: ${result.filename}`, 'success');
        } else {
            showError(result.message);
            clearImage();
        }
    } catch (error) {
        showError(`Upload failed: ${error.message}`);
        clearImage();
    }
}

// eslint-disable-next-line no-unused-vars
function clearImage() {
    document.getElementById('uploadArea').style.display = 'flex';
    document.getElementById('imagePreview').style.display = 'none';
    document.getElementById('runPipelineBtn').style.display = 'none';
    document.getElementById('imageUpload').value = '';
    uploadedImageFilename = null;
}

// eslint-disable-next-line no-unused-vars
async function runImageTest() {
    if (!uploadedImageFilename) {
        showError('No image uploaded');
        return;
    }

    appendOutput('[INFO] Starting full pipeline test...', 'info');
    appendOutput('[INFO] Processing strobed ball image through: Ball Detection → Spin Analysis → Shot Calculation', 'info');

    // Run the test_uploaded_image tool
    await runTool('test_uploaded_image');
}