// Configuration Manager JavaScript

let currentConfig = {};
let defaultConfig = {};
let userSettings = {};
let categories = {};
let basicSubcategories = {};
let configMetadata = {};
const modifiedSettings = new Set();
let ws = null;

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
    initWebSocket();
    loadConfiguration();
});

// Initialize WebSocket connection
function initWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${window.location.host}/ws`);

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);

        if (data.type === 'config_update') {
            updateStatus(`Configuration updated: ${data.key}`, 'success');
            if (data.requires_restart) {
                updateStatus('Restart required for changes to take effect', 'warning');
            }
        } else if (data.type === 'config_reset') {
            updateStatus('Configuration reset to defaults', 'success');
            loadConfiguration();
        }
    };

    ws.onerror = (error) => {
        console.error('WebSocket error:', error);
        updateStatus('WebSocket connection error', 'error');
    };
}

// Load configuration from server
async function loadConfiguration() {
    try {
        modifiedSettings.clear();
        updateModifiedCount();

        // Load all configuration data in parallel
        const [configRes, defaultsRes, userRes, categoriesRes, subcategoriesRes, metadataRes] = await Promise.all([
            fetch('/api/config'),
            fetch('/api/config/defaults'),
            fetch('/api/config/user'),
            fetch('/api/config/categories'),
            fetch('/api/config/basic-subcategories'),
            fetch('/api/config/metadata')
        ]);

        const configData = await configRes.json();
        const defaultsData = await defaultsRes.json();
        const userData = await userRes.json();
        categories = await categoriesRes.json();
        basicSubcategories = await subcategoriesRes.json();
        configMetadata = await metadataRes.json();

        currentConfig = configData.data || {};
        defaultConfig = defaultsData.data || {};
        userSettings = userData.data || {};

        renderCategories();
        renderConfiguration();
        updateModifiedCount();

        updateConditionalVisibility();
        setTimeout(updateConditionalVisibility, 100);

        updateStatus('Configuration loaded', 'success');
    } catch (error) {
        console.error('Failed to load configuration:', error);
        updateStatus('Failed to load configuration', 'error');
    }
}

// Render category list
function renderCategories() {
    const categoryList = document.getElementById('categoryList');
    categoryList.innerHTML = '';

    // Ensure Basic category appears first if it exists
    const categoryOrder = ['Basic'];
    Object.keys(categories).forEach(cat => {
        if (cat !== 'Basic') {
            categoryOrder.push(cat);
        }
    });

    // Add "All Settings" option first
    const allItem = document.createElement('li');
    allItem.className = 'category-item';
    allItem.dataset.category = 'all';
    allItem.textContent = 'All Settings';
    allItem.onclick = () => selectCategory('all');
    categoryList.appendChild(allItem);

    // Add categories in order
    categoryOrder.forEach(category => {
        if (categories[category]) {
            const li = document.createElement('li');
            li.className = 'category-item';
            li.dataset.category = category;
            li.textContent = category + ` (${categories[category].length})`;
            li.onclick = () => selectCategory(category);
            categoryList.appendChild(li);
        }
    });

    // Select Basic category by default (without adding active class here)
    setTimeout(() => {
        if (categories['Basic']) {
            selectCategory('Basic');
        } else {
            selectCategory('all');
        }
    }, 100);
}

// Select category
function selectCategory(category) {
    // Update active category
    document.querySelectorAll('.category-item').forEach(item => {
        item.classList.remove('active');
        if (item.dataset.category === category) {
            item.classList.add('active');
        }
    });

    // For Basic category, re-render with subcategories
    if (category === 'Basic') {
        renderConfiguration('Basic');
    } else if (category === 'all') {
        // Show all categories
        renderConfiguration();
        document.querySelectorAll('.config-group').forEach(group => {
            group.style.display = 'block';
        });
    } else {
        // Filter to show only selected category
        renderConfiguration();
        document.querySelectorAll('.config-group').forEach(group => {
            group.style.display = group.dataset.category === category ? 'block' : 'none';
        });
    }
}

// Render configuration UI
function renderConfiguration(selectedCategory = null) {
    const content = document.getElementById('configContent');
    content.innerHTML = '';

    // Special handling for Basic category with subcategories
    if (selectedCategory === 'Basic' && basicSubcategories && Object.keys(basicSubcategories).length > 0) {
        // Render Basic settings grouped by subcategory
        Object.entries(basicSubcategories).forEach(([subcategory, keys]) => {
            const group = document.createElement('div');
            group.className = 'config-group';
            group.dataset.category = 'Basic';
            group.dataset.subcategory = subcategory;

            const title = document.createElement('h3');
            title.className = 'config-group-title';
            title.textContent = subcategory;
            group.appendChild(title);

            keys.forEach(key => {
                const value = getNestedValue(currentConfig, key);
                const defaultValue = getNestedValue(defaultConfig, key);
                const isModified = getNestedValue(userSettings, key) !== undefined;

                const item = createConfigItem(key, value, defaultValue, isModified);
                group.appendChild(item);
            });

            content.appendChild(group);
        });
    } else {
        Object.entries(categories).forEach(([category, keys]) => {
            if (selectedCategory && selectedCategory !== 'all' && selectedCategory !== category) {
                return;
            }

            const basicKeys = [];
            const advancedKeys = [];

            keys.forEach(key => {
                const metadata = configMetadata[key] || {};
                if (metadata.showInBasic) {
                    basicKeys.push(key);
                } else {
                    advancedKeys.push(key);
                }
            });

            const group = document.createElement('div');
            group.className = 'config-group';
            group.dataset.category = category;

            const title = document.createElement('h3');
            title.className = 'config-group-title';
            title.textContent = category;
            if (category === 'Basic') {
                title.innerHTML = category + ' <span style="color: var(--warning);">★</span>';
            }
            group.appendChild(title);

            // Render basic settings first (if any)
            if (basicKeys.length > 0 && category !== 'Basic') {
                const basicHeader = document.createElement('div');
                basicHeader.className = 'config-section-header';
                basicHeader.innerHTML = '<span class="section-label">Essential Settings</span>';
                group.appendChild(basicHeader);

                basicKeys.forEach(key => {
                    const value = getNestedValue(currentConfig, key);
                    const defaultValue = getNestedValue(defaultConfig, key);
                    const isModified = getNestedValue(userSettings, key) !== undefined;

                    const item = createConfigItem(key, value, defaultValue, isModified);
                    group.appendChild(item);
                });
            } else if (category === 'Basic') {
                // For Basic category, all settings are basic by definition
                keys.forEach(key => {
                    const value = getNestedValue(currentConfig, key);
                    const defaultValue = getNestedValue(defaultConfig, key);
                    const isModified = getNestedValue(userSettings, key) !== undefined;

                    const item = createConfigItem(key, value, defaultValue, isModified);
                    group.appendChild(item);
                });
            }

            // Render advanced settings (if any and not in Basic category)
            if (advancedKeys.length > 0 && category !== 'Basic') {
                if (basicKeys.length > 0) {
                    const advancedHeader = document.createElement('div');
                    advancedHeader.className = 'config-section-header';
                    advancedHeader.innerHTML = '<span class="section-label">Advanced Settings</span>';
                    group.appendChild(advancedHeader);
                }

                advancedKeys.forEach(key => {
                    const value = getNestedValue(currentConfig, key);
                    const defaultValue = getNestedValue(defaultConfig, key);
                    const isModified = getNestedValue(userSettings, key) !== undefined;

                    const item = createConfigItem(key, value, defaultValue, isModified);
                    group.appendChild(item);
                });
            }

            content.appendChild(group);
        });
    }
}

// Create configuration item element
function createConfigItem(key, value, defaultValue, isModified) {
    const item = document.createElement('div');
    item.className = 'config-item';
    if (isModified) {
        item.classList.add('modified');
    }
    item.dataset.key = key;

    const metadata = configMetadata[key] || {};

    if (metadata.visibleWhen && !checkVisibilityCondition(metadata.visibleWhen)) {
        item.style.display = 'none';
        item.dataset.hiddenByCondition = 'true';
    }

    // Label
    const label = document.createElement('div');
    label.className = 'config-label';

    // Use display name from metadata or extract readable name from key
    const displayName = metadata.displayName || (() => {
        const parts = key.split('.');
        const name = parts[parts.length - 1]
            .replace(/^k/, '')
            .replace(/([A-Z])/g, ' $1')
            .trim();
        return name;
    })();

    // Build label HTML with optional description
    let labelHTML = `<div class="config-label-name">${displayName}</div>`;
    if (metadata.description) {
        labelHTML += `<div class="config-description">${metadata.description}</div>`;
    }
    labelHTML += `<span class="key">${key}</span>`;

    label.innerHTML = labelHTML;
    item.appendChild(label);

    // Input
    const inputContainer = document.createElement('div');
    inputContainer.className = 'input-container';

    const input = createInput(key, value);
    input.className = 'config-input';
    input.dataset.key = key;
    input.dataset.original = String(value);
    if (input.tagName === 'SELECT') {
        input.onchange = () => handleValueChange(key, input.value, input.dataset.original);
    } else {
        input.oninput = () => handleValueChange(key, input.value, input.dataset.original);
    }
    inputContainer.appendChild(input);

    if (key === 'cameras.slot1.type' || key === 'cameras.slot2.type' ||
        key === 'cameras.slot1_type' || key === 'cameras.slot2_type') {
        inputContainer.style.display = 'flex';
        inputContainer.style.alignItems = 'center';
        inputContainer.style.gap = '0.75rem';

        const detectBtn = document.createElement('button');
        detectBtn.className = 'btn btn-secondary btn-small';
        detectBtn.textContent = 'Detect';
        detectBtn.style.flexShrink = '0';
        detectBtn.title = 'Auto-detect connected camera';
        detectBtn.onclick = async () => {
            detectBtn.disabled = true;
            const originalText = detectBtn.textContent;
            detectBtn.textContent = 'Detecting...';
            try {
                await detectAndSetCameras(key);
            } finally {
                detectBtn.disabled = false;
                detectBtn.textContent = originalText;
            }
        };
        inputContainer.appendChild(detectBtn);
    }

    item.appendChild(inputContainer);

    // Actions
    const actions = document.createElement('div');
    actions.className = 'config-actions';

    if (isModified) {
        const resetBtn = document.createElement('button');
        resetBtn.className = 'btn btn-secondary btn-small';
        resetBtn.textContent = 'Reset';
        resetBtn.onclick = () => resetValue(key);
        actions.appendChild(resetBtn);
    }

    item.appendChild(actions);

    return item;
}

// Create appropriate input based on value type
function createInput(key, value) {
    const metadata = configMetadata[key] || {};

    if (metadata.type === 'select' && metadata.options) {
        const select = document.createElement('select');
        Object.entries(metadata.options).forEach(([optValue, optDisplay]) => {
            const option = document.createElement('option');
            option.value = optValue;
            option.textContent = optDisplay;
            if (value === optValue) {
                option.selected = true;
            }
            select.appendChild(option);
        });
        return select;
    }

    // Handle arrays and complex objects
    if (Array.isArray(value) || (typeof value === 'object' && value !== null)) {
        const textarea = document.createElement('textarea');
        textarea.value = JSON.stringify(value, null, 2);
        textarea.rows = 3;
        textarea.style.width = '100%';
        textarea.style.fontFamily = 'Monaco, Menlo, monospace';
        textarea.style.fontSize = '0.875rem';
        return textarea;
    } else if (typeof value === 'boolean' || value === '0' || value === '1') {
        const select = document.createElement('select');
        select.innerHTML = `
            <option value="true" ${value === true || value === '1' ? 'selected' : ''}>True</option>
            <option value="false" ${value === false || value === '0' ? 'selected' : ''}>False</option>
        `;
        return select;
    } else if (typeof value === 'number' || !isNaN(value)) {
        const input = document.createElement('input');
        input.type = 'number';
        input.value = value;

        // Set constraints based on key patterns
        if (key.includes('Port')) {
            input.min = 1;
            input.max = 65535;
        } else if (key.includes('Gain')) {
            input.min = 0.5;
            input.max = 16;
            input.step = 0.1;
        }

        return input;
    } else {
        const input = document.createElement('input');
        input.type = 'text';
        input.value = value || '';
        return input;
    }
}

// Handle value change
async function handleValueChange(key, currentValue, originalValue) {
    try {
        let current = currentValue;
        let original = originalValue;

        if (current === 'true') current = true;
        else if (current === 'false') current = false;
        else if (!isNaN(current) && current !== '') current = Number(current);

        if (original === 'true') original = true;
        else if (original === 'false') original = false;
        else if (!isNaN(original) && original !== '') original = Number(original);

        const isModified = current !== original;

        setNestedValue(currentConfig, key, current);

        if (key === 'system.mode') {
            updateConditionalVisibility();
        }

        const item = document.querySelector(`[data-key="${key}"]`);
        if (item) {
            if (isModified) {
                modifiedSettings.add(key);
                item.classList.add('modified');
            } else {
                modifiedSettings.delete(key);
                item.classList.remove('modified');
            }
        }

        updateModifiedCount();

        if (isModified) {
            updateStatus(`Modified: ${key}`, 'success');
        }
    } catch (error) {
        console.error('Failed to handle value change:', error);
        updateStatus('Failed to update value', 'error');
    }
}

// Save all changes
async function saveChanges() {
    if (modifiedSettings.size === 0) {
        updateStatus('No changes to save', 'warning');
        return;
    }

    updateStatus('Saving changes...', '');

    const errors = [];
    const requiresRestart = [];

    for (const key of modifiedSettings) {
        const input = document.querySelector(`[data-key="${key}"] .config-input`);
        if (!input) continue;

        let value = input.value;

        // Convert value type
        if (value === 'true') value = true;
        else if (value === 'false') value = false;
        else if (!isNaN(value) && value !== '') value = Number(value);

        try {
            const response = await fetch(`/api/config/${key}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ value })
            });

            const result = await response.json();

            if (result.error) {
                errors.push(`${key}: ${result.error}`);
            } else if (result.requires_restart) {
                requiresRestart.push(key);
            }
        } catch (error) {
            errors.push(`${key}: ${error.message}`);
        }
    }

    if (errors.length > 0) {
        updateStatus(`Errors: ${errors.join(', ')}`, 'error');
    } else {
        modifiedSettings.clear();
        updateModifiedCount();

        if (requiresRestart.length > 0) {
            updateStatus('Changes saved. Restart required for some settings.', 'warning');
        } else {
            updateStatus('All changes saved successfully', 'success');
        }

        // Reload to get fresh data
        setTimeout(() => loadConfiguration(), 1000);
    }
}

// Reset single value
async function resetValue(key) {
    try {
        const defaultValue = getNestedValue(defaultConfig, key);

        const response = await fetch(`/api/config/${key}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ value: defaultValue })
        });

        const result = await response.json();

        if (result.error) {
            updateStatus(`Failed to reset: ${result.error}`, 'error');
        } else {
            updateStatus(`Reset ${key} to default`, 'success');
            modifiedSettings.delete(key);
            updateModifiedCount();

            // Update UI
            const item = document.querySelector(`[data-key="${key}"]`);
            if (item) {
                item.classList.remove('modified');
                const input = item.querySelector('.config-input');
                if (input) {
                    input.value = defaultValue;
                }
            }
        }
    } catch (error) {
        console.error('Failed to reset value:', error);
        updateStatus('Failed to reset value', 'error');
    }
}

// Reset all to defaults
function resetAll() {
    showConfirm(
        'Reset All Settings',
        'Are you sure you want to reset all settings to defaults? This cannot be undone.',
        async () => {
            try {
                const response = await fetch('/api/config/reset', {
                    method: 'POST'
                });

                const result = await response.json();

                if (result.success) {
                    updateStatus('All settings reset to defaults', 'success');
                    modifiedSettings.clear();
                    loadConfiguration();
                } else {
                    updateStatus(`Failed to reset: ${result.message}`, 'error');
                }
            } catch (error) {
                console.error('Failed to reset all:', error);
                updateStatus('Failed to reset configuration', 'error');
            }
        }
    );
}

// Reload configuration
async function reloadConfig() {
    updateStatus('Reloading configuration...', '');
    await loadConfiguration();
}

// Show differences
async function showDiff() {
    try {
        const response = await fetch('/api/config/diff');
        const result = await response.json();
        const diff = result.data || {};

        if (Object.keys(diff).length === 0) {
            updateStatus('No differences from defaults', '');
            return;
        }

        // Format diff for display
        let diffHtml = '<h3>Configuration Differences</h3><ul>';
        Object.entries(diff).forEach(([key, values]) => {
            diffHtml += `<li><strong>${key}</strong><br>`;
            diffHtml += `Default: ${JSON.stringify(values.default)}<br>`;
            diffHtml += `Current: ${JSON.stringify(values.user)}</li>`;
        });
        diffHtml += '</ul>';

        showModal('Configuration Differences', diffHtml);
    } catch (error) {
        console.error('Failed to get diff:', error);
        updateStatus('Failed to get differences', 'error');
    }
}

// Export configuration
async function exportConfig() {
    try {
        const response = await fetch('/api/config/export');
        const config = await response.json();

        // Create download
        const blob = new Blob([JSON.stringify(config, null, 2)], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `pitrac_config_${new Date().toISOString().split('T')[0]}.json`;
        a.click();
        URL.revokeObjectURL(url);

        updateStatus('Configuration exported', 'success');
    } catch (error) {
        console.error('Failed to export:', error);
        updateStatus('Failed to export configuration', 'error');
    }
}

// Import configuration
function importConfig() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';

    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;

        try {
            const text = await file.text();
            const config = JSON.parse(text);

            const response = await fetch('/api/config/import', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(config)
            });

            const result = await response.json();

            if (result.success) {
                updateStatus('Configuration imported successfully', 'success');
                loadConfiguration();
            } else {
                updateStatus(`Import failed: ${result.message}`, 'error');
            }
        } catch (error) {
            console.error('Failed to import:', error);
            updateStatus('Failed to import configuration', 'error');
        }
    };

    input.click();
}

// Filter configuration
function filterConfig() {
    const searchTerm = document.getElementById('searchInput').value.toLowerCase();

    document.querySelectorAll('.config-item').forEach(item => {
        const key = item.dataset.key.toLowerCase();
        const label = item.querySelector('.config-label').textContent.toLowerCase();

        if (key.includes(searchTerm) || label.includes(searchTerm)) {
            item.style.display = 'grid';
        } else {
            item.style.display = 'none';
        }
    });
}

// Utility functions
function getNestedValue(obj, path) {
    return path.split('.').reduce((current, key) => current?.[key], obj);
}

function setNestedValue(obj, path, value) {
    const parts = path.split('.');
    let current = obj;
    for (let i = 0; i < parts.length - 1; i++) {
        const part = parts[i];
        if (!(part in current) || typeof current[part] !== 'object') {
            current[part] = {};
        }
        current = current[part];
    }
    current[parts[parts.length - 1]] = value;
}

function updateStatus(message, type = '') {
    const statusEl = document.getElementById('statusMessage');
    statusEl.textContent = message;
    statusEl.className = 'status-message ' + type;
}

function updateModifiedCount() {
    document.getElementById('modifiedCount').textContent = modifiedSettings.size;
}

function showModal(title, body) {
    document.getElementById('modalTitle').textContent = title;
    document.getElementById('modalBody').innerHTML = body;
    document.getElementById('confirmModal').classList.add('active');
}

function showConfirm(title, message, onConfirm) {
    document.getElementById('modalTitle').textContent = title;
    document.getElementById('modalBody').textContent = message;

    const confirmBtn = document.getElementById('modalConfirmBtn');
    confirmBtn.onclick = () => {
        closeModal();
        onConfirm();
    };

    document.getElementById('confirmModal').classList.add('active');
}

function closeModal() {
    document.getElementById('confirmModal').classList.remove('active');
}

async function detectAndSetCameras(targetKey = null) {
    try {
        updateStatus('Detecting cameras...', 'info');

        const response = await fetch('/api/cameras/detect');
        const result = await response.json();

        if (result.success && result.cameras && result.cameras.length > 0) {
            const config = result.configuration;

            if (targetKey === 'cameras.slot1.type') {
                const input = document.querySelector('[data-key="cameras.slot1.type"]');
                if (input) {
                    input.value = config.slot1.type;
                    handleValueChange('cameras.slot1.type', config.slot1.type, input.dataset.original);
                }
                updateStatus(`Camera 1 detected: Type ${config.slot1.type}`, 'success');
            } else if (targetKey === 'cameras.slot2.type') {
                const input = document.querySelector('[data-key="cameras.slot2.type"]');
                if (input) {
                    input.value = config.slot2.type;
                    handleValueChange('cameras.slot2.type', config.slot2.type, input.dataset.original);
                }
                updateStatus(`Camera 2 detected: Type ${config.slot2.type}`, 'success');
            } else {
                const input1 = document.querySelector('[data-key="cameras.slot1.type"]');
                const input2 = document.querySelector('[data-key="cameras.slot2.type"]');

                if (input1) {
                    input1.value = config.slot1.type;
                    handleValueChange('cameras.slot1.type', config.slot1.type, input1.dataset.original);
                }
                if (input2) {
                    input2.value = config.slot2.type;
                    handleValueChange('cameras.slot2.type', config.slot2.type, input2.dataset.original);
                }

                updateStatus(`Detected cameras - Slot 1: Type ${config.slot1.type}, Slot 2: Type ${config.slot2.type}`, 'success');
            }

        } else {
            const errorMsg = result.message || 'No cameras detected';
            updateStatus(`Camera detection failed: ${errorMsg}`, 'error');

            if (result.warnings && result.warnings.length > 0) {
                showModal('Camera Detection Failed',
                    `<p><strong>${errorMsg}</strong></p>` +
                    '<p>Warnings:</p>' +
                    '<ul style="text-align: left; margin: 10px 20px;">' +
                    result.warnings.map(w => `<li>${w}</li>`).join('') +
                    '</ul>' +
                    '<p style="margin-top: 15px;">Troubleshooting:</p>' +
                    '<ul style="text-align: left; margin: 10px 20px;">' +
                    '<li>Check ribbon cable connections and orientation</li>' +
                    '<li>Verify camera_auto_detect=1 in /boot/firmware/config.txt</li>' +
                    '<li>Power cycle the Raspberry Pi</li>' +
                    '<li>Ensure cameras are compatible (IMX296 recommended)</li>' +
                    '</ul>'
                );
            }
        }
    } catch (error) {
        console.error('Camera detection error:', error);
        updateStatus('Failed to detect cameras - check connection', 'error');
        showModal('Connection Error',
            '<p>Failed to connect to camera detection service.</p>' +
            `<p>Error: ${error.message}</p>` +
            '<p style="margin-top: 15px;">Please ensure:</p>' +
            '<ul style="text-align: left; margin: 10px 20px;">' +
            '<li>The PiTrac web service is running</li>' +
            '<li>You have a stable network connection</li>' +
            '<li>Try refreshing the page</li>' +
            '</ul>'
        );
    }
}

function checkVisibilityCondition(condition) {
    for (const [condKey, condValue] of Object.entries(condition)) {
        const actualValue = getNestedValue(currentConfig, condKey);
        if (actualValue !== condValue) {
            return false;
        }
    }
    return true;
}

function updateConditionalVisibility() {
    document.querySelectorAll('.config-item').forEach(item => {
        const key = item.dataset.key;
        const metadata = configMetadata[key];

        if (metadata && metadata.visibleWhen) {
            const shouldBeVisible = checkVisibilityCondition(metadata.visibleWhen);
            if (shouldBeVisible) {
                item.style.display = '';
                delete item.dataset.hiddenByCondition;
            } else {
                item.style.display = 'none';
                item.dataset.hiddenByCondition = 'true';
            }
        }
    });
}
