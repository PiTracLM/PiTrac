---
layout: default
title: Configuration Management
parent: Development Guide
nav_order: 3
---

# Configuration Management

## Web UI Configuration Interface

### Accessing Configuration

1. Start the web server: `pitrac web start`
2. Navigate to `http://your-pi-ip:8080`
3. Click on the **Configuration** section

### Configuration Features

#### Organized Categories
Settings are organized into logical categories:
- **System** - Pi mode, golfer orientation, practice ball mode
- **Cameras** - Camera types, lens configurations, gain settings
- **Ball Detection** - Detection methods (HoughCircles, YOLO, YOLO+SAHI)
- **AI Detection** - ONNX model selection with auto-discovery
- **Simulators** - E6, GSPro, TruGolf connectivity settings
- **Storage** - Image logging, diagnostic levels
- **Network** - Broker addresses, port configurations
- **Logging** - Log levels, debug options
- **Strobing** - IR strobe timing and intensity
- **Spin Analysis** - Spin calculation parameters
- **Calibration** - Camera calibration data
- **Club Data** - Club selection and specifications
- **Display** - UI and output preferences

#### User Interface Features
- **Search Functionality** - Real-time search across all settings
- **Basic/Advanced Views** - Simplified interface for basic users
- **Live Validation** - Real-time validation with error messages
- **Change Tracking** - Shows modified settings count
- **Save/Reset Options** - Batch save or reset to defaults
- **Import/Export** - Backup and restore configurations
- **Diff View** - Compare current settings with defaults
- **Restart Notifications** - Warns when changes require restart

### Three-Tier Configuration System

1. **System Defaults** - Built-in defaults from the application
2. **Calibration Data** - Persistent camera calibration parameters
3. **User Overrides** - User-specific customizations

## Configuration Files (Backend)

While configuration is managed through the web UI, understanding the file structure helps with debugging:

### User Configuration
- **Location**: `~/.pitrac/config/`
- **Format**: YAML files for user overrides
- **Managed by**: Web UI automatically

### Configuration Hierarchy

Settings are resolved in priority order:
1. User overrides (via web UI)
2. Calibration data
3. System defaults

## Web UI Configuration API

The web server provides REST APIs for configuration:

```javascript
// Get all configuration
GET /api/config

// Get specific category
GET /api/config/cameras

// Update settings
PUT /api/config
{
  "cameras.camera1_gain": 2.5,
  "system.putting_mode": true
}

// Get defaults
GET /api/config/defaults

// Reset to defaults
POST /api/config/reset

// Import configuration
POST /api/config/import

// Export configuration
GET /api/config/export
```

## Development: ConfigurationManager (C++)

For developers working on the core C++ code:

```cpp
#include "configuration_manager.h"

// Get singleton instance
ConfigurationManager& config = ConfigurationManager::GetInstance();

// Load configuration (auto-loads YAML overrides)
config.LoadConfigFile("golf_sim_config.json");

// Get values with defaults
float gain = config.GetFloat("gs_config.cameras.kCamera1Gain", 2.0f);
bool putting = config.GetBool("gs_config.modes.kStartInPuttingMode", false);

// Values automatically include web UI overrides
```

## Environment Variables

Environment variables can override configuration (rarely needed):

```bash
# Override message broker address
export PITRAC_MSG_BROKER_FULL_ADDRESS=tcp://localhost:61616

# Override camera types
export PITRAC_SLOT1_CAMERA_TYPE=4
export PITRAC_SLOT2_CAMERA_TYPE=4
```

## Configuration Workflow

1. **Access web UI** - Navigate to Configuration section
2. **Search or browse** - Find settings to modify
3. **Make changes** - Edit values with live validation
4. **Review changes** - See highlighted modifications
5. **Save changes** - Click Save to apply
6. **Restart if needed** - Web UI indicates if restart required

## Best Practices

1. **Use the web UI exclusively** - Don't edit files manually
2. **Test changes incrementally** - Make small changes and test
3. **Export before major changes** - Backup configuration
4. **Use Basic view for simple changes** - Advanced view can be overwhelming
5. **Check validation messages** - Web UI validates all inputs

## Troubleshooting Configuration

### Changes Not Taking Effect

1. Check if restart is required (web UI will indicate)
2. Verify changes were saved (not just entered)
3. Check for validation errors in web UI
4. Review logs for configuration loading errors

### Configuration Reset

If configuration becomes corrupted:

1. Use web UI "Reset to Defaults" button
2. Or manually: `rm -rf ~/.pitrac/config/*`
3. Restart web server: `pitrac web restart`

### Viewing Active Configuration

The web UI shows:
- Current values (with source indicator)
- Modified values (highlighted)
- Default values (in diff view)

The web UI configuration system provides a superior user experience with validation, organization, and real-time feedback compared to manual file editing.