---
layout: default
title: Configuration Management
parent: Development Guide
nav_order: 3
---

# Configuration Management

PiTrac uses a hierarchical configuration system that supports JSON defaults with YAML overrides.

## Configuration Hierarchy

Settings are resolved in this priority order:
1. Command-line arguments (highest)
2. Environment variables
3. YAML configuration files
4. JSON configuration file (lowest)

## Configuration Files

### golf_sim_config.json

The primary configuration file containing all system parameters.

**Location**: `Software/LMSourceCode/ImageProcessing/golf_sim_config.json`

**Structure**:
```json
{
  "gs_config": {
    "logging": { ... },
    "modes": { ... },
    "ball_identification": { ... },
    "ball_position": { ... },
    "calibration": { ... },
    "cameras": { ... },
    "golf_simulator_interfaces": { ... },
    "image_capture": { ... },
    "spin_analysis": { ... }
  }
}
```

### pitrac.yaml

User-friendly YAML configuration for overriding JSON defaults.

**Search locations** (in order):
1. `./pitrac.yaml`
2. `~/.pitrac/config/pitrac.yaml`
3. `/etc/pitrac/config/pitrac.yaml`
4. `/etc/pitrac/pitrac.yaml`

**Example**:
```yaml
cameras:
  camera1_gain: 2.5      # Overrides gs_config.cameras.kCamera1Gain
  camera2_gain: 3.0      # Overrides gs_config.cameras.kCamera2Gain

system:
  putting_mode: true     # Overrides gs_config.modes.kStartInPuttingMode

ball_detection:
  use_clahe: false       # Overrides gs_config.ball_identification.kUseCLAHEProcessing
  clahe_clip_limit: 12   # Overrides gs_config.ball_identification.kCLAHEClipLimit

simulators:
  e6_host: "10.0.0.100"  # Overrides gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress
  e6_port: 5000          # Overrides gs_config.golf_simulator_interfaces.E6.kE6ConnectPort
```

### parameter-mappings.yaml

Maps user-friendly YAML keys to JSON configuration paths.

**Location**: `/etc/pitrac/config/parameter-mappings.yaml`

**Structure**:
```yaml
mappings:
  cameras.camera1_gain:
    json_path: gs_config.cameras.kCamera1Gain
    type: float
    validation:
      min: 0.5
      max: 16.0
```

## ConfigurationManager API

The core class that handles configuration loading and resolution.

### Usage

```cpp
#include "configuration_manager.h"

// Get singleton instance
ConfigurationManager& config = ConfigurationManager::GetInstance();

// Initialize with configuration files
config.Initialize("golf_sim_config.json", "pitrac.yaml");

// Get values (uses full hierarchy resolution)
float gain = config.GetFloat("gs_config.cameras.kCamera1Gain", 1.0f);
bool use_clahe = config.GetBool("gs_config.ball_identification.kUseCLAHEProcessing", true);
std::string host = config.GetString("gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress", "");

// Check value source
std::string source = config.GetValueSource("gs_config.cameras.kCamera1Gain");
// Returns: "yaml", "json", "cli", or "not_found"
```

### Integration with GolfSimConfiguration

The existing `SetConstant` methods check ConfigurationManager first:

```cpp
void GolfSimConfiguration::SetConstant(const std::string& tag_name, float& constant_value) {
    ConfigurationManager& config_mgr = ConfigurationManager::GetInstance();
    if (config_mgr.HasKey(tag_name)) {
        float val = config_mgr.GetFloat(tag_name, constant_value);
        if (val != constant_value) {
            constant_value = val;
            return;
        }
    }
    // Fall back to JSON
    constant_value = configuration_root_.get<float>(tag_name, 0.0);
}
```

## Environment Variables

### Camera Hardware
```bash
export PITRAC_SLOT1_CAMERA_TYPE=4    # 1-5
export PITRAC_SLOT2_CAMERA_TYPE=4    # 1-5
export PITRAC_SLOT1_LENS_TYPE=1      # Lens type
export PITRAC_SLOT2_LENS_TYPE=1      # Lens type
```

Camera types:
- 1 = Pi Camera v1.3
- 2 = Pi Camera v2
- 3 = Pi HQ Camera
- 4 = Pi Global Shutter
- 5 = InnoMaker CAM-MIPI327RAW

### System Paths
```bash
export PITRAC_ROOT=/path/to/pitrac
export PITRAC_BASE_IMAGE_LOGGING_DIR=$HOME/LM_Shares/Images/
export PITRAC_WEBSERVER_SHARE_DIR=$HOME/LM_Shares/WebShare/
export PITRAC_MSG_BROKER_FULL_ADDRESS=tcp://127.0.0.1:61616
```

## CLI Integration

The `pitrac` CLI parses YAML configuration and sets environment variables:

```bash
pitrac run                          # Uses default configuration
pitrac run --config my-config.yaml  # Use specific config file
pitrac run --system-mode camera2    # Override system mode
```

The CLI extracts camera slot configuration from YAML and exports as environment variables before starting the binary.

## Available Parameters

### Camera Settings
- `cameras.camera1_gain` → `gs_config.cameras.kCamera1Gain`
- `cameras.camera2_gain` → `gs_config.cameras.kCamera2Gain`

### System Modes
- `system.putting_mode` → `gs_config.modes.kStartInPuttingMode`

### Ball Detection
- `ball_detection.method` → `gs_config.ball_identification.kDetectionMethod`
- `ball_detection.use_clahe` → `gs_config.ball_identification.kUseCLAHEProcessing`
- `ball_detection.clahe_clip_limit` → `gs_config.ball_identification.kCLAHEClipLimit`

### Simulator Interfaces
- `simulators.e6_host` → `gs_config.golf_simulator_interfaces.E6.kE6ConnectAddress`
- `simulators.e6_port` → `gs_config.golf_simulator_interfaces.E6.kE6ConnectPort`
- `simulators.gspro_host` → `gs_config.golf_simulator_interfaces.GSPro.kGSProConnectAddress`
- `simulators.gspro_port` → `gs_config.golf_simulator_interfaces.GSPro.kGSProConnectPort`

### AI Detection (Experimental)
- `ai_detection.model_path` → `gs_config.ball_identification.kONNXModelPath`
- `ai_detection.confidence_threshold` → `gs_config.ball_identification.kONNXConfidenceThreshold`
- `ai_detection.nms_threshold` → `gs_config.ball_identification.kONNXNMSThreshold`

## Adding New Parameters

1. **Add to golf_sim_config.json**:
```json
{
  "gs_config": {
    "my_feature": {
      "kNewParameter": "default_value"
    }
  }
}
```

2. **Add mapping in parameter-mappings.yaml**:
```yaml
mappings:
  my_feature.new_parameter:
    json_path: gs_config.my_feature.kNewParameter
    type: string
    validation:
      min: 0
      max: 100
```

3. **Use in code**:
```cpp
float value;
SetConstant("gs_config.my_feature.kNewParameter", value);
```

## Implementation Details

### Reverse Mapping
ConfigurationManager builds a bidirectional mapping cache on initialization. When code requests a JSON path like `gs_config.cameras.kCamera1Gain`, it:
1. Maps the JSON path to the YAML key `cameras.camera1_gain`
2. Looks for that key in the YAML configuration
3. Returns the YAML value if found, otherwise falls back to JSON

### File Loading
1. JSON config loaded via boost::property_tree
2. YAML parsed with yaml-cpp and converted to property_tree
3. Parameter mappings loaded and reverse cache built
4. Environment variables checked at runtime

## Summary

PiTrac's configuration system provides flexible override capabilities through YAML files while maintaining compatibility with the existing JSON-based system. Users can override any mapped parameter by creating a pitrac.yaml file with the appropriate settings.