# PiTrac Configuration Schema Documentation

This document describes the structure and options for configuration objects in `configurations.json`.

## Configuration Object Format

Each configuration entry is a key-value pair where the key is a dot-notation path (e.g., `gs_config.cameras.kCamera1Gain`) and the value is an object with the following properties:

### Required Fields

| Field | Type | Description |
|-------|------|-------------|
| `category` | string | Main category for grouping settings |
| `displayName` | string | Human-readable name shown in UI |
| `description` | string | Detailed explanation of the setting |
| `type` | string | Data type: `select`, `boolean`, `number`, `text`, `path` |
| `default` | varies | Default value (type depends on `type` field) |

### Optional Fields

| Field | Type | Description |
|-------|------|-------------|
| `showInBasic` | boolean | Whether to show in basic/simple configuration view |
| `basicSubcategory` | string | Subcategory for basic view organization |
| `requiresRestart` | boolean | Whether changing this setting requires system restart |
| `visibleWhen` | object | Conditional visibility based on other settings |
| `affectsSettings` | array | List of other settings affected by this one |

### Type-Specific Fields

#### For `type: "select"`
| Field | Type | Description |
|-------|------|-------------|
| `options` | array | Array of valid option values |

#### For `type: "number"`
| Field | Type | Description |
|-------|------|-------------|
| `min` | number | Minimum allowed value |
| `max` | number | Maximum allowed value |
| `step` | number | Increment step for UI controls |

## Categories

Available categories for organizing settings:
- `System` - Core system configuration
- `Cameras` - Camera hardware and settings
- `Ball Detection` - Ball tracking algorithms
- `AI Detection` - Neural network settings
- `Simulators` - Golf simulator interfaces
- `Strobing` - LED strobe configuration
- `Storage` - File paths and directories
- `Logging` - Debug and logging options
- `Network` - Network and messaging configuration
- `Spin Analysis` - Spin calculation settings
- `Advanced` - Expert-level parameters

## Conditional Visibility

Use `visibleWhen` to show/hide settings based on other configuration values:

```json
"visibleWhen": {
  "system.mode": "single"  // Only visible in single Pi mode
}
```

## Setting Dependencies

Use `affectsSettings` to indicate which other settings are impacted:

```json
"affectsSettings": ["cameras.slot2.type", "cameras.slot2.lens", "gs_config.cameras.kCamera2Gain"]
```

## Example Configuration Entry

```json
"gs_config.cameras.kCamera1SearchCenterX": {
  "category": "Cameras",
  "showInBasic": false,
  "displayName": "Camera 1 Search Center X",
  "description": "X coordinate for ball search center in Camera 1",
  "type": "number",
  "min": 0,
  "max": 1920,
  "step": 10,
  "default": 850,
  "requiresRestart": true
}
```

## Basic View Subcategories

Settings shown in basic view are organized into subcategories with display order:
1. System
2. Cameras  
3. Simulators
4. Ball Detection
5. AI Detection
6. Storage
7. Logging
8. Network
9. Advanced

## Notes

- pitrac_lm reads every setting from the merged config served at `/api/internal/config`
- The `requiresRestart` flag triggers automatic process restart when changed via web UI