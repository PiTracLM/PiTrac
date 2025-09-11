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
| `passedVia` | string | How parameter is passed: `cli`, `environment`, or omitted (config file) |
| `passedTo` | string | Target component(s): `camera1`, `camera2`, `both`, `system` |
| `cliArgument` | string | Command-line argument name (when `passedVia: "cli"`) |
| `envVariable` | string | Environment variable name (when `passedVia: "environment"`) |
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

## Parameter Passing Methods

### 1. CLI Arguments (`passedVia: "cli"`)
Parameters passed as command-line arguments to the pitrac_lm binary.
- Requires `cliArgument` field
- Format: `--parameter_name=value` or `--flag` (for booleans)
- Target specified by `passedTo` field

### 2. Environment Variables (`passedVia: "environment"`)
Parameters set as environment variables before process startup.
- Requires `envVariable` field  
- Format: `PITRAC_VARIABLE_NAME=value`
- Target specified by `passedTo` field

### 3. Config File (no `passedVia` field)
Parameters written to `/etc/pitrac/golf_sim_config.json`.
- Default method when `passedVia` is omitted
- Accessible to all components

## Target Components (`passedTo`)

| Value | Description | Usage |
|-------|-------------|--------|
| `camera1` | Camera 1 process only | Settings specific to first camera |
| `camera2` | Camera 2 process only | Settings specific to second camera (single Pi mode) |
| `both` | Both camera processes | Settings that apply to both cameras |
| `system` | System-wide | Non-camera settings (simulators, storage, etc.) |

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
  "requiresRestart": true,
  "passedVia": "cli",
  "passedTo": "camera1",
  "cliArgument": "--search_center_x"
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

- Settings without `passedVia` are stored in the config file and accessible to all components
- Boolean CLI arguments are passed as flags without values (e.g., `--practice_ball`)
- Environment variables are set per-process when `passedTo` specifies a target
- The `requiresRestart` flag triggers automatic process restart when changed via web UI