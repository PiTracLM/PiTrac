# PiTrac Configuration System Documentation

## Overview

PiTrac uses a three-tier configuration system that provides both simplicity for casual users and flexibility for advanced tuning:

1. **Default Configuration** - Built-in values and `golf_sim_config.json`
2. **User Configuration** - YAML overrides in `~/.pitrac/config/pitrac.yaml`
3. **Runtime Configuration** - Command-line flags for temporary overrides

## Quick Start

### Basic Users (90% of users)
```bash
# Edit basic settings (recommended)
pitrac config edit

# Apply a preset for your environment
pitrac config preset indoor

# Start PiTrac with your configuration
pitrac run
```

### Advanced Users
```bash
# Edit advanced settings
pitrac config edit --advanced

# Fine-tune specific parameters
pitrac config set ball_detection.clahe_clip_limit 10

# Validate your configuration
pitrac config validate
```

## Configuration Files

### File Locations
- **System Templates**: `/etc/pitrac/config/`
  - `settings-basic.yaml` - Basic user settings template
  - `settings-advanced.yaml` - Advanced settings template
  - `parameter-mappings.yaml` - Maps user settings to technical parameters
  - `golf_sim_config.json` - Complete technical configuration

- **User Configuration**: `~/.pitrac/config/`
  - `pitrac.yaml` - Your personal configuration overrides
  - `backups/` - Automatic backups of your configuration

## Configuration Modes

### Basic Mode (Default)
Contains ~25 essential parameters that most users need:
- System setup (single/dual camera, golfer orientation)
- Camera hardware selection
- Simulator connections
- Ball detection method
- Storage options

### Advanced Mode
Includes all basic settings plus ~75 technical parameters:
- HoughCircles detection tuning
- AI detection parameters
- Camera gain/contrast fine-tuning
- Spin analysis settings
- Strobe timing adjustments

## Common Settings

### System Configuration
```yaml
system:
  mode: single              # single or dual camera
  golfer_orientation: right_handed  # or left_handed
  putting_mode: false       # Start in putting mode
```

### Camera Setup
```yaml
cameras:
  slot1_type: 4            # 1=PiCam1.3, 2=PiCam2, 3=PiHQ, 4=PiGS, 5=InnoMaker
  camera1_gain: 1.0        # 0.5-16.0 (higher = brighter)
```

### Simulator Integration
```yaml
simulators:
  e6_host: "192.168.1.100"    # E6 Connect IP
  gspro_host: "10.0.0.47"     # GSPro IP
```

### Ball Detection
```yaml
ball_detection:
  method: legacy           # legacy, experimental, experimental_sahi
  use_clahe: true         # Contrast enhancement
  clahe_clip_limit: 8     # 1-40 (higher = more contrast)
```

## Presets

PiTrac includes optimized presets for common scenarios:

### Indoor
```bash
pitrac config preset indoor
```
- Optimized for artificial lighting
- Enhanced contrast processing
- Adjusted detection thresholds

### Outdoor/Garage
```bash
pitrac config preset outdoor
```
- Natural light optimization
- Alternative detection algorithm
- Modified edge detection

### Putting
```bash
pitrac config preset putting
```
- Putting mode enabled
- Adjusted strobe timing
- Skip spin calculation

### High-Speed Driver
```bash
pitrac config preset driver
```
- Optimized for fast ball speeds
- Increased detection circles
- Enhanced trajectory tracking

### Debug Mode
```bash
pitrac config preset debug
```
- All diagnostic logging enabled
- Save all images
- Write analysis CSV files

## Command-Line Tools

### Edit Configuration
```bash
# Edit basic settings (default)
pitrac config edit

# Edit advanced settings
pitrac config edit --advanced

# Use specific editor
pitrac config edit --editor vim
```

### Get/Set Values
```bash
# Get a configuration value
pitrac config get ball_detection.method

# Set a configuration value
pitrac config set ball_detection.method experimental

# Set with validation
pitrac config set cameras.camera1_gain 2.5
```

### Validate Configuration
```bash
# Check for errors
pitrac config validate

# Show effective configuration
pitrac config show --effective

# Show differences from defaults
pitrac config diff
```

### Backup/Restore
```bash
# Create backup
pitrac config backup

# Restore from backup
pitrac config restore 20250819_143022

# Reset to defaults
pitrac config reset --backup
```

## Advanced Topics

### Parameter Mappings

User-friendly settings are automatically mapped to technical parameters:

| User Setting | Technical Parameter | JSON Path |
|-------------|-------------------|-----------|
| `system.putting_mode` | `kStartInPuttingMode` | `gs_config.modes.kStartInPuttingMode` |
| `ball_detection.method` | `kDetectionMethod` | `gs_config.ball_identification.kDetectionMethod` |
| `cameras.camera1_gain` | `kCamera1Gain` | `gs_config.cameras.kCamera1Gain` |

### Validation Rules

Each parameter has validation rules:
- **Type**: string, integer, float, boolean
- **Range**: min/max values for numeric parameters
- **Enum**: Allowed values for categorical parameters
- **Pattern**: Regex patterns for strings (e.g., IP addresses)

Example:
```yaml
cameras.camera1_gain:
  type: float
  min: 0.5
  max: 16.0
```

### Override Hierarchy

Settings are resolved in this order (highest priority first):
1. Command-line flags (`--camera-gain 2.0`)
2. User YAML config (`~/.pitrac/config/pitrac.yaml`)
3. System YAML config (`/etc/pitrac/config/pitrac.yaml`)
4. JSON defaults (`golf_sim_config.json`)
5. Built-in defaults (hardcoded)

### Environment Variables

Some settings can be overridden via environment variables:
```bash
export PITRAC_SLOT1_CAMERA_TYPE=4
export PITRAC_MSG_BROKER_FULL_ADDRESS=tcp://localhost:61616
```

## Troubleshooting

### Configuration Not Loading
```bash
# Check syntax
pitrac config validate

# View parsing errors
pitrac config show --debug
```

### Settings Not Taking Effect
```bash
# Check which source is being used
pitrac config source ball_detection.method

# Show effective configuration
pitrac config show --effective
```

### Reset Configuration
```bash
# Reset to defaults (creates backup)
pitrac config reset --backup

# Remove all user configuration
rm -rf ~/.pitrac/config/
```

## Migration from Old System

If you have an existing `golf_sim_config.json` with custom values:

1. **Automatic Migration** (recommended):
   ```bash
   pitrac config migrate
   ```

2. **Manual Migration**:
   - Compare your JSON values with defaults
   - Add overrides to `pitrac.yaml`
   - Test with `pitrac config validate`

## Best Practices

1. **Start with Basic Mode**: Only switch to advanced if needed
2. **Use Presets**: Start with a preset, then customize
3. **Validate Changes**: Always run `validate` after editing
4. **Keep Backups**: Configuration is automatically backed up
5. **Document Changes**: Add comments in YAML for future reference

## Parameter Reference

### Basic Parameters (~25)
See `/etc/pitrac/config/settings-basic.yaml` for complete list with descriptions.

### Advanced Parameters (~75)
See `/etc/pitrac/config/settings-advanced.yaml` for complete list with descriptions.

### Technical Parameters (~250)
See `golf_sim_config.json` for all technical parameters (not recommended to edit directly).

## Examples

### Example: Optimize for Bright Room
```yaml
# ~/.pitrac/config/pitrac.yaml
ball_detection:
  use_clahe: false  # Disable contrast enhancement
  
ball_detection_advanced:
  strobed_balls:
    canny_lower: 40  # Increase threshold
    canny_upper: 100
```

### Example: Dual Camera Setup
```yaml
system:
  mode: dual
  camera_role: camera1  # This Pi is camera 1

network:
  broker_address: tcp://192.168.1.50:61616
```

### Example: Simulator Integration
```yaml
simulators:
  gspro_host: "10.0.0.47"
  
# Fine-tune for simulator
spin:
  write_csv_files: false  # Don't need debug files
  
storage:
  log_exposure_images: false  # Save disk space
```

## Support

For configuration help:
1. Check this documentation
2. Run `pitrac config validate` for specific errors
3. Visit the [PiTrac Discord](https://discord.gg/j9YWCMFVHN)
4. File issues at [GitHub](https://github.com/pitraclm/pitrac)

## Version History

- **v2.0** - Three-tier configuration system with YAML
- **v1.0** - Original JSON-only configuration