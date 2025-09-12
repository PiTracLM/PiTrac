// Configuration Manager JavaScript
/* global saveChanges, resetAll, reloadConfig, showDiff, 
   filterConfig, closeModal, setTheme, openImage, resetShot, controlPiTrac, 
   resetValueFromDiff, resetAllFromDiff, clearSearch, searchConfig,
   resetToDefault */

let currentConfig = {};
let defaultConfig = {};
let userSettings = {};
let categories = {};
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
        const [configRes, defaultsRes, userRes, categoriesRes, metadataRes] = await Promise.all([
            fetch('/api/config'),
            fetch('/api/config/defaults'),
            fetch('/api/config/user'),
            fetch('/api/config/categories'),
            fetch('/api/config/metadata')
        ]);

        const configData = await configRes.json();
        const defaultsData = await defaultsRes.json();
        const userData = await userRes.json();
        categories = await categoriesRes.json();
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

    // Add "All Settings" option first
    const allItem = document.createElement('li');
    allItem.className = 'category-item';
    allItem.dataset.category = 'all';
    allItem.textContent = 'All Settings';
    allItem.onclick = () => selectCategory('all');
    categoryList.appendChild(allItem);

    // Add each category with its settings count
    Object.keys(categories).forEach(category => {
        const categoryData = categories[category];
        const basicCount = categoryData.basic ? categoryData.basic.length : 0;
        const advancedCount = categoryData.advanced ? categoryData.advanced.length : 0;
        const totalCount = basicCount + advancedCount;
        
        if (totalCount > 0) {
            const li = document.createElement('li');
            li.className = 'category-item';
            li.dataset.category = category;
            li.textContent = `${category} (${totalCount})`;
            li.onclick = () => selectCategory(category);
            categoryList.appendChild(li);
        }
    });

    // Select 'all' by default
    setTimeout(() => {
        selectCategory('all');
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

    // Render configuration for selected category
    if (category === 'all') {
        renderConfiguration();
    } else {
        renderConfiguration(category);
    }
}

// Render configuration UI
function renderConfiguration(selectedCategory = null) {
    const content = document.getElementById('configContent');
    content.innerHTML = '';
    
    // Determine which categories to render
    const categoriesToRender = selectedCategory && selectedCategory !== 'all' 
        ? { [selectedCategory]: categories[selectedCategory] }
        : categories;

    if (selectedCategory === null || selectedCategory === 'all') {
        const hasBasicSettings = Object.entries(categoriesToRender).some(([_, categoryData]) => 
            categoryData && categoryData.basic && categoryData.basic.length > 0
        );
        
        if (hasBasicSettings) {
            const basicSection = document.createElement('div');
            basicSection.className = 'config-section';
            
            const basicHeader = document.createElement('div');
            basicHeader.className = 'config-main-section-header';
            basicHeader.innerHTML = '<h2>Basic Settings</h2>';
            basicSection.appendChild(basicHeader);
            
            Object.entries(categoriesToRender).forEach(([category, categoryData]) => {
                if (!categoryData || !categoryData.basic || categoryData.basic.length === 0) return;
                
                const group = document.createElement('div');
                group.className = 'config-group';
                group.dataset.category = category;
                
                const title = document.createElement('h3');
                title.className = 'config-group-title';
                title.textContent = category;
                group.appendChild(title);
                
                categoryData.basic.forEach(key => {
                    const value = getNestedValue(currentConfig, key);
                    const defaultValue = getNestedValue(defaultConfig, key);
                    const isModified = getNestedValue(userSettings, key) !== undefined;
                    
                    const item = createConfigItem(key, value, defaultValue, isModified);
                    group.appendChild(item);
                });
                
                basicSection.appendChild(group);
            });
            
            content.appendChild(basicSection);
        }
        
        const hasAdvancedSettings = Object.entries(categoriesToRender).some(([_, categoryData]) => 
            categoryData && categoryData.advanced && categoryData.advanced.length > 0
        );
        
        if (hasAdvancedSettings) {
            const advancedSection = document.createElement('div');
            advancedSection.className = 'config-section';
            
            const advancedHeader = document.createElement('div');
            advancedHeader.className = 'config-main-section-header';
            advancedHeader.innerHTML = '<h2>Advanced Settings</h2>';
            advancedSection.appendChild(advancedHeader);
            
            Object.entries(categoriesToRender).forEach(([category, categoryData]) => {
                if (!categoryData || !categoryData.advanced || categoryData.advanced.length === 0) return;
                
                const group = document.createElement('div');
                group.className = 'config-group';
                group.dataset.category = category;
                
                const title = document.createElement('h3');
                title.className = 'config-group-title';
                title.textContent = category;
                group.appendChild(title);
                
                categoryData.advanced.forEach(key => {
                    const value = getNestedValue(currentConfig, key);
                    const defaultValue = getNestedValue(defaultConfig, key);
                    const isModified = getNestedValue(userSettings, key) !== undefined;
                    
                    const item = createConfigItem(key, value, defaultValue, isModified);
                    group.appendChild(item);
                });
                
                advancedSection.appendChild(group);
            });
            
            content.appendChild(advancedSection);
        }
    } else {
        Object.entries(categoriesToRender).forEach(([category, categoryData]) => {
            if (!categoryData) return;
            
            const group = document.createElement('div');
            group.className = 'config-group';
            group.dataset.category = category;

            const title = document.createElement('h3');
            title.className = 'config-group-title';
            title.textContent = category;
            group.appendChild(title);

            if (categoryData.basic && categoryData.basic.length > 0) {
                const basicHeader = document.createElement('div');
                basicHeader.className = 'config-section-header';
                basicHeader.innerHTML = '<span class="section-label">Basic Settings</span>';
                group.appendChild(basicHeader);

                categoryData.basic.forEach(key => {
                    const value = getNestedValue(currentConfig, key);
                    const defaultValue = getNestedValue(defaultConfig, key);
                    const isModified = getNestedValue(userSettings, key) !== undefined;

                    const item = createConfigItem(key, value, defaultValue, isModified);
                    group.appendChild(item);
                });
            }

            if (categoryData.advanced && categoryData.advanced.length > 0) {
                if (categoryData.basic && categoryData.basic.length > 0) {
                    const advancedHeader = document.createElement('div');
                    advancedHeader.className = 'config-section-header';
                    advancedHeader.innerHTML = '<span class="section-label">Advanced Settings</span>';
                    group.appendChild(advancedHeader);
                }

                categoryData.advanced.forEach(key => {
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
    
    const isUserSet = getNestedValue(userSettings, key) !== undefined;
    
    if (isUserSet) {
        item.classList.add('user-set');
    } else {
        item.classList.add('using-default');
    }
    
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

    let labelHTML = `<div class="config-label-name">`;
    labelHTML += displayName;
    
    // Note: isUserSet already defined above
    if (!isUserSet) {
        labelHTML += ` <span class="default-badge" title="Using default value">DEFAULT</span>`;
    }
    
    if (metadata.requiresRestart) {
        labelHTML += ` <span class="restart-indicator" title="Restart required for changes to take effect">[Restart Required]</span>`;
    }
    
    labelHTML += `</div>`;
    
    if (metadata.description) {
        labelHTML += `<div class="config-description">${metadata.description}</div>`;
    }
    labelHTML += `<span class="key">${key}</span>`;

    label.innerHTML = labelHTML;
    item.appendChild(label);

    // Input
    const inputContainer = document.createElement('div');
    inputContainer.className = 'input-container';

    const input = createInput(key, value, defaultValue, isUserSet);
    input.className = 'config-input';
    input.dataset.key = key;
    input.dataset.original = String(value);
    input.dataset.default = String(defaultValue);
    
    if (!isUserSet) {
        input.classList.add('default-value');
        if (input.tagName === 'INPUT' && input.type === 'text') {
            input.placeholder = `Default: ${defaultValue}`;
        }
    }
    
    const inputWrapper = document.createElement('div');
    inputWrapper.className = 'input-wrapper';
    inputWrapper.appendChild(input);
    
    if (isUserSet || isModified) {
        const clearBtn = document.createElement('button');
        clearBtn.className = 'clear-value-btn';
        clearBtn.innerHTML = '×';
        clearBtn.title = 'Reset to default';
        clearBtn.onclick = (e) => {
            e.preventDefault();
            resetValue(key);
        };
        inputWrapper.appendChild(clearBtn);
    }
    
    const validationError = document.createElement('div');
    validationError.className = 'validation-error';
    validationError.style.display = 'none';
    
    const validateAndUpdate = async () => {
        const isValid = await validateInput(key, input.value, validationError);
        if (isValid) {
            handleValueChange(key, input.value, input.dataset.original);
            
            if (input.value !== defaultValue) {
                input.classList.remove('default-value');
                item.classList.remove('using-default');
                item.classList.add('user-set');
                
                if (!inputWrapper.querySelector('.clear-value-btn')) {
                    const clearBtn = document.createElement('button');
                    clearBtn.className = 'clear-value-btn';
                    clearBtn.innerHTML = '×';
                    clearBtn.title = 'Reset to default';
                    clearBtn.onclick = (e) => {
                        e.preventDefault();
                        resetValue(key);
                    };
                    inputWrapper.appendChild(clearBtn);
                }
            }
        }
    };
    
    if (input.tagName === 'SELECT') {
        input.onchange = validateAndUpdate;
    } else {
        input.oninput = validateAndUpdate;
    }
    
    inputContainer.appendChild(inputWrapper);
    inputContainer.appendChild(validationError);

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

    // Only show reset button for user-set values (not for defaults)
    if (isUserSet && !isModified) {
        const resetBtn = document.createElement('button');
        resetBtn.className = 'btn btn-secondary btn-small';
        resetBtn.textContent = 'Reset';
        resetBtn.title = 'Reset to default value';
        resetBtn.onclick = () => resetValue(key);
        actions.appendChild(resetBtn);
    }

    item.appendChild(actions);

    return item;
}

// Create appropriate input based on value type
function createInput(key, value, defaultValue, isUserSet) {
    const metadata = configMetadata[key] || {};

    if (key.includes('ONNXModelPath') || key.includes('onnx_model')) {
        const select = document.createElement('select');

        if (metadata.options && Object.keys(metadata.options).length > 0) {
            Object.entries(metadata.options).forEach(([modelName, modelPath]) => {
                const option = document.createElement('option');
                option.value = modelPath;
                option.textContent = modelName;
                if (modelPath === value) {
                    option.selected = true;
                }
                select.appendChild(option);
            });
        } else {
            if (value) {
                const option = document.createElement('option');
                option.value = value;
                const parts = value.split('/');
                let displayName = 'Unknown Model';
                for (let i = parts.length - 2; i >= 0; i--) {
                    if (parts[i] && parts[i] !== 'weights' && parts[i] !== 'best.onnx') {
                        displayName = parts[i];
                        break;
                    }
                }
                option.textContent = displayName;
                option.selected = true;
                select.appendChild(option);
            }
        }
        
        return select;
    }

    if (metadata.type === 'select' && metadata.options) {
        const select = document.createElement('select');
        Object.entries(metadata.options).forEach(([optValue, optDisplay]) => {
            const option = document.createElement('option');
            option.value = optValue;
            option.textContent = optDisplay;
            if (String(value) === String(optValue)) {
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
        if (!isUserSet) {
            textarea.placeholder = `Default: ${JSON.stringify(defaultValue, null, 2)}`;
        }
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
        if (!isUserSet && defaultValue !== undefined) {
            input.placeholder = `Default: ${defaultValue}`;
        }
        return input;
    }
}

// Handle value change
async function handleValueChange(key, currentValue, originalValue) {
    try {
        let current = currentValue;
        let original = originalValue;
        const defaultValue = getNestedValue(defaultConfig, key);

        if (current === 'true') current = true;
        else if (current === 'false') current = false;
        else if (!isNaN(current) && current !== '') current = Number(current);

        if (original === 'true') original = true;
        else if (original === 'false') original = false;
        else if (!isNaN(original) && original !== '') original = Number(original);
        
        let defaultVal = defaultValue;
        if (defaultVal === 'true' || defaultVal === '1') defaultVal = true;
        else if (defaultVal === 'false' || defaultVal === '0') defaultVal = false;
        else if (!isNaN(defaultVal) && defaultVal !== '') defaultVal = Number(defaultVal);

        const isModified = current !== original;
        const isDifferentFromDefault = current !== defaultVal;

        setNestedValue(currentConfig, key, current);
        
        if (isDifferentFromDefault) {
            setNestedValue(userSettings, key, current);
        } else {
            deleteNestedValue(userSettings, key);
        }

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
            
            if (isDifferentFromDefault) {
                item.classList.remove('using-default');
                item.classList.add('user-set');
                
                const badge = item.querySelector('.default-badge');
                if (badge) badge.remove();
                
                const inputWrapper = item.querySelector('.input-wrapper');
                if (inputWrapper && !inputWrapper.querySelector('.clear-value-btn')) {
                    const clearBtn = document.createElement('button');
                    clearBtn.className = 'clear-value-btn';
                    clearBtn.innerHTML = '×';
                    clearBtn.title = 'Reset to default';
                    clearBtn.onclick = (e) => {
                        e.preventDefault();
                        resetValue(key);
                    };
                    inputWrapper.appendChild(clearBtn);
                }
                
                const input = item.querySelector('.config-input');
                if (input) {
                    input.classList.remove('default-value');
                }
            } else {
                item.classList.remove('user-set');
                item.classList.add('using-default');
                
                let badge = item.querySelector('.default-badge');
                if (!badge) {
                    const labelName = item.querySelector('.config-label-name');
                    if (labelName && !labelName.querySelector('.default-badge')) {
                        const badgeHtml = ' <span class="default-badge" title="Using default value">DEFAULT</span>';
                        labelName.insertAdjacentHTML('beforeend', badgeHtml);
                    }
                }
                
                const clearBtn = item.querySelector('.clear-value-btn');
                if (clearBtn) clearBtn.remove();
                
                const input = item.querySelector('.config-input');
                if (input) {
                    input.classList.add('default-value');
                }
            }
        }

        updateModifiedCount();

        if (isModified) {
            updateStatus(`Modified: ${key}`, 'info');
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
    let savedCount = 0;
    let resetCount = 0;

    for (const key of modifiedSettings) {
        const input = document.querySelector(`.config-input[data-key="${key}"]`);
        if (!input) continue;

        let value = input.value;
        const defaultValue = getNestedValue(defaultConfig, key);

        // Convert value type
        if (value === 'true') value = true;
        else if (value === 'false') value = false;
        else if (!isNaN(value) && value !== '') value = Number(value);
        
        let defaultVal = defaultValue;
        if (defaultVal === 'true' || defaultVal === '1') defaultVal = true;
        else if (defaultVal === 'false' || defaultVal === '0') defaultVal = false;
        else if (!isNaN(defaultVal) && defaultVal !== '') defaultVal = Number(defaultVal);

        try {
            const response = await fetch(`/api/config/${key}`, {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ value })
            });

            const result = await response.json();

            if (result.error) {
                errors.push(`${key}: ${result.error}`);
            } else {
                if (input) {
                    input.dataset.original = String(value);
                }
                
                const item = document.querySelector(`[data-key="${key}"]`);
                if (item) {
                    item.classList.remove('modified');
                }
                
                if (value === defaultVal) {
                    resetCount++;
                } else {
                    savedCount++;
                }
                
                if (result.requires_restart) {
                    requiresRestart.push(key);
                }
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

        let message = '';
        if (savedCount > 0) {
            message += `Saved ${savedCount} custom setting${savedCount !== 1 ? 's' : ''}`;
        }
        if (resetCount > 0) {
            if (message) message += ', ';
            message += `Reset ${resetCount} to default${resetCount !== 1 ? 's' : ''}`;
        }
        
        if (requiresRestart.length > 0) {
            updateStatus(message + '. Restart required for some settings.', 'warning');
        } else {
            updateStatus(message || 'All changes saved successfully', 'success');
        }
    }
}

function resetToDefault(key) {
    resetValue(key);
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
            
            deleteNestedValue(userSettings, key);

            // Update UI
            const item = document.querySelector(`[data-key="${key}"]`);
            if (item) {
                item.classList.remove('modified', 'user-set');
                item.classList.add('using-default');
                
                const input = item.querySelector('.config-input');
                if (input) {
                    input.value = defaultValue;
                    input.classList.add('default-value');
                    if (input.tagName === 'INPUT' && input.type === 'text') {
                        input.placeholder = `Default: ${defaultValue}`;
                    }
                }
                
                let badge = item.querySelector('.default-badge');
                if (!badge) {
                    const labelName = item.querySelector('.config-label-name');
                    if (labelName) {
                        const badgeHtml = ' <span class="default-badge" title="Using default value">DEFAULT</span>';
                        labelName.insertAdjacentHTML('beforeend', badgeHtml);
                    }
                }
                
                const clearBtn = item.querySelector('.clear-value-btn');
                if (clearBtn) {
                    clearBtn.remove();
                }
                
                const actions = item.querySelector('.config-actions');
                if (actions) {
                    actions.innerHTML = '';
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

        let diffHtml = `
            <div class="diff-viewer">
                <div class="diff-header">
                    <h3>Configuration Differences</h3>
                    <p class="diff-summary">${Object.keys(diff).length} settings modified from defaults</p>
                </div>
                <div class="diff-content">
                    <table class="diff-table">
                        <thead>
                            <tr>
                                <th>Setting</th>
                                <th>Default Value</th>
                                <th>Your Value</th>
                                <th>Action</th>
                            </tr>
                        </thead>
                        <tbody>
        `;
        
        Object.entries(diff).forEach(([key, values]) => {
            const defaultVal = formatValue(values.default);
            const userVal = formatValue(values.user);
            const metadata = configMetadata[key] || {};
            const displayName = metadata.displayName || key.split('.').pop();
            
            diffHtml += `
                <tr class="diff-row">
                    <td class="diff-key">
                        <div class="diff-key-name">${displayName}</div>
                        <div class="diff-key-path">${key}</div>
                    </td>
                    <td class="diff-default">
                        <code>${defaultVal}</code>
                    </td>
                    <td class="diff-user">
                        <code>${userVal}</code>
                    </td>
                    <td class="diff-actions">
                        <button class="btn btn-small" onclick="resetValueFromDiff('${key}')">Reset</button>
                    </td>
                </tr>
            `;
        });
        
        diffHtml += `
                        </tbody>
                    </table>
                </div>
                <div class="diff-footer">
                    <button class="btn btn-primary" onclick="closeModal()">Close</button>
                    <button class="btn btn-danger" onclick="resetAllFromDiff()">Reset All to Defaults</button>
                </div>
            </div>
        `;

        showModal('Configuration Differences', diffHtml);
    } catch (error) {
        console.error('Failed to get diff:', error);
        updateStatus('Failed to get differences', 'error');
    }
}

function formatValue(value) {
    if (value === null) return 'null';
    if (value === undefined) return 'undefined';
    if (typeof value === 'boolean') return value ? 'true' : 'false';
    if (typeof value === 'string') return `"${value}"`;
    if (typeof value === 'object') return JSON.stringify(value, null, 2);
    return String(value);
}

async function resetValueFromDiff(key) {
    await resetValue(key);
    closeModal();
    showDiff(); 
}

function resetAllFromDiff() {
    closeModal();
    resetAll();
}

function searchConfig() {
    const searchTerm = document.getElementById('searchInput').value.toLowerCase();
    
    if (!searchTerm) {
        document.querySelectorAll('.config-item').forEach(item => {
            item.style.display = 'grid';
        });
        document.querySelectorAll('.config-group').forEach(group => {
            group.style.display = 'block';
        });
        return;
    }

    let hasVisibleItems = false;
    
    document.querySelectorAll('.config-group').forEach(group => {
        group.style.display = 'none';
    });
    
    document.querySelectorAll('.config-item').forEach(item => {
        const key = item.dataset.key.toLowerCase();
        const labelName = item.querySelector('.config-label-name')?.textContent.toLowerCase() || '';
        const description = item.querySelector('.config-description')?.textContent.toLowerCase() || '';
        
        if (key.includes(searchTerm) || labelName.includes(searchTerm) || description.includes(searchTerm)) {
            item.style.display = 'grid';
            const parentGroup = item.closest('.config-group');
            if (parentGroup) {
                parentGroup.style.display = 'block';
            }
            hasVisibleItems = true;
        } else {
            item.style.display = 'none';
        }
    });
    
    if (!hasVisibleItems) {
        updateStatus('No settings found matching: ' + searchTerm, 'warning');
    }
}

function clearSearch() {
    document.getElementById('searchInput').value = '';
    searchConfig();
}

function filterConfig() {
    searchConfig();
}

async function validateInput(key, value, errorElement) {
    try {
        const metadata = configMetadata[key] || {};
        
        if (metadata.type === 'number') {
            const num = parseFloat(value);
            if (isNaN(num)) {
                errorElement.textContent = 'Must be a valid number';
                errorElement.style.display = 'block';
                return false;
            }
            if (metadata.min !== undefined && num < metadata.min) {
                errorElement.textContent = `Minimum value is ${metadata.min}`;
                errorElement.style.display = 'block';
                return false;
            }
            if (metadata.max !== undefined && num > metadata.max) {
                errorElement.textContent = `Maximum value is ${metadata.max}`;
                errorElement.style.display = 'block';
                return false;
            }
        }
        
        errorElement.style.display = 'none';
        errorElement.textContent = '';
        
        checkDependencies(key, value);
        
        return true;
    } catch (error) {
        console.error('Validation error:', error);
        return true; // Don't block on validation errors
    }
}

function checkDependencies(key, value) {
    const dependencies = {
        'system.mode': {
            'single': ['cameras.slot2 settings will be used for dual camera on single Pi'],
            'dual_primary': ['This Pi will act as primary camera. Ensure secondary Pi is configured.'],
            'dual_secondary': ['This Pi will act as secondary camera. Ensure primary Pi is configured.']
        },
        'cameras.slot1.type': {
            '*': ['Camera type change may require recalibration']
        },
        'cameras.slot2.type': {
            '*': ['Camera type change may require recalibration']
        },
        'network.broker_address': {
            '*': ['Changing broker address will affect camera communication']
        }
    };
    
    const keyDeps = dependencies[key];
    if (keyDeps) {
        const warnings = keyDeps[value] || keyDeps['*'] || [];
        if (warnings.length > 0) {
            showDependencyWarning(key, warnings);
        }
    }
}

function showDependencyWarning(key, warnings) {
    const message = `<strong>Changing ${key} affects:</strong><br>` + warnings.join('<br>');
    
    const notification = document.createElement('div');
    notification.className = 'dependency-warning';
    notification.innerHTML = message;
    notification.style.cssText = `
        position: fixed;
        top: 80px;
        right: 20px;
        background: var(--warning-bg, #fef3c7);
        color: var(--warning-text, #92400e);
        padding: 1rem;
        border-radius: 8px;
        box-shadow: 0 4px 6px rgba(0,0,0,0.1);
        max-width: 300px;
        z-index: 1000;
        animation: slideIn 0.3s ease;
    `;
    
    document.body.appendChild(notification);
    
    setTimeout(() => {
        notification.style.animation = 'slideOut 0.3s ease';
        setTimeout(() => notification.remove(), 300);
    }, 5000);
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

function deleteNestedValue(obj, path) {
    const parts = path.split('.');
    if (parts.length === 1) {
        delete obj[parts[0]];
        return;
    }
    
    let current = obj;
    for (let i = 0; i < parts.length - 1; i++) {
        if (!(parts[i] in current)) {
            return; // Path doesn't exist
        }
        current = current[parts[i]];
    }
    
    delete current[parts[parts.length - 1]];
    
    let parent = obj;
    for (let i = 0; i < parts.length - 1; i++) {
        const nextParent = parent[parts[i]];
        if (nextParent && Object.keys(nextParent).length === 0) {
            delete parent[parts[i]];
            break;
        }
        parent = nextParent;
    }
}

function updateStatus(message, type = '') {
    const statusEl = document.getElementById('statusMessage');
    statusEl.textContent = message;
    statusEl.className = 'status-message ' + type;
}

function updateModifiedCount() {
    const modifiedCount = modifiedSettings.size;
    document.getElementById('modifiedCount').textContent = modifiedCount;
    
    const saveBtn = document.getElementById('saveBtn');
    if (saveBtn) {
        saveBtn.disabled = modifiedCount === 0;
    }
    
    let userSetCount = 0;
    const countUserSettings = (obj, depth = 0) => {
        if (depth > 10) return;
        for (const key in obj) {
            if (typeof obj[key] === 'object' && obj[key] !== null && !Array.isArray(obj[key])) {
                countUserSettings(obj[key], depth + 1);
            } else {
                userSetCount++;
            }
        }
    };
    countUserSettings(userSettings);
    
    const totalSettings = document.querySelectorAll('.config-item').length;
    const defaultCount = totalSettings - userSetCount;
    
    let counterEl = document.getElementById('settingsCounter');
    if (!counterEl) {
        const statusBar = document.querySelector('.status-bar');
        if (statusBar) {
            counterEl = document.createElement('div');
            counterEl.id = 'settingsCounter';
            counterEl.className = 'settings-counter';
            statusBar.insertBefore(counterEl, statusBar.firstChild);
        }
    }
    
    if (counterEl) {
        counterEl.innerHTML = `
            <span class="counter-custom" title="Settings you've customized">${userSetCount} custom</span>
            <span class="counter-default" title="Settings using default values">${defaultCount} defaults</span>
            <span class="counter-total" title="Total number of settings">${totalSettings} total</span>
        `;
    }
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
                    const typeValue = String(config.slot1.type);
                    input.value = typeValue;
                    handleValueChange('cameras.slot1.type', typeValue, input.dataset.original);
                }
                updateStatus(`Camera 1 detected: Type ${config.slot1.type}`, 'success');
            } else if (targetKey === 'cameras.slot2.type') {
                const input = document.querySelector('[data-key="cameras.slot2.type"]');
                if (input) {
                    const typeValue = String(config.slot2.type);
                    input.value = typeValue;
                    handleValueChange('cameras.slot2.type', typeValue, input.dataset.original);
                }
                updateStatus(`Camera 2 detected: Type ${config.slot2.type}`, 'success');
            } else {
                const input1 = document.querySelector('[data-key="cameras.slot1.type"]');
                const input2 = document.querySelector('[data-key="cameras.slot2.type"]');

                if (input1) {
                    const typeValue = String(config.slot1.type);
                    input1.value = typeValue;
                    handleValueChange('cameras.slot1.type', typeValue, input1.dataset.original);
                }
                if (input2) {
                    const typeValue = String(config.slot2.type);
                    input2.value = typeValue;
                    handleValueChange('cameras.slot2.type', typeValue, input2.dataset.original);
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
        let actualValue = getNestedValue(currentConfig, condKey);
        // If value is not in currentConfig, use the default value
        if (actualValue === undefined) {
            actualValue = getNestedValue(defaultConfig, condKey);
        }
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

window.saveChanges = saveChanges;
window.reloadConfig = reloadConfig;
window.showDiff = showDiff;
window.resetAll = resetAll;
window.searchConfig = searchConfig;
window.clearSearch = clearSearch;
window.resetToDefault = resetToDefault;
window.resetValueFromDiff = resetValueFromDiff;
window.resetAllFromDiff = resetAllFromDiff;
