---
title: Configuration
layout: default
nav_order: 3
parent: Software
---

# Configuration

This document covers configuration files and environment variables for PiTrac.

## Environment Variables

PiTrac requires several environment variables to be set. Add these to your shell configuration file (`.zshrc`, `.bashrc`, etc.):  

### Required Variables

```bash
# Core paths
export PITRAC_ROOT=/Dev/PiTrac/Software/LMSourceCode
export PITRAC_BASE_IMAGE_LOGGING_DIR=~/LM_Shares/Images/
export PITRAC_WEBSERVER_SHARE_DIR=~/LM_Shares/WebShare/
export PITRAC_MSG_BROKER_FULL_ADDRESS=tcp://10.0.0.41:61616

# Camera configuration
export LIBCAMERA_RPI_CONFIG_FILE=/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml

# Camera types (choose based on your hardware)
export PITRAC_SLOT1_CAMERA_TYPE=4  # Official Pi GS cameras with 6mm lenses
export PITRAC_SLOT2_CAMERA_TYPE=4

# Alternative: Innomaker GS cameras with 3.6mm lenses
#export PITRAC_SLOT1_CAMERA_TYPE=6
#export PITRAC_SLOT2_CAMERA_TYPE=6
```

### Optional Variables

```bash
# Golf simulator integration (uncomment as needed)
#export PITRAC_E6_HOST_ADDRESS=10.0.0.29
#export PITRAC_GSPRO_HOST_ADDRESS=10.0.0.29
```

## Configuration Files

### golf_sim_config.json

The main configuration file located at `$PITRAC_ROOT/ImageProcessing/golf_sim_config.json`.

#### Key Sections

##### Golf Simulator Interfaces

```json
{
  "golf_simulator_interfaces": {
    "E6": {
      "kE6ConnectAddress": "10.0.0.10",
      "kE6ConnectPort": "2483", 
      "kE6InterMessageDelayMs": 50
    }
  }
}
```

##### Camera Configuration

The configuration file contains camera-specific settings for:
- Image processing parameters
- Calibration values
- Trigger timing
- Image capture settings

##### Web Server Settings

```json
{
  "kWebServerTomcatShareDirectory": "/home/username/LM_Shares/WebShare/"
}
```

## Camera Configuration Files

### rpi_apps.yaml

Location: `/usr/share/libcamera/pipeline/rpi/pisp/rpi_apps.yaml` (Pi 5) or `/usr/share/libcamera/pipeline/rpi/vc4/rpi_apps.yaml` (Pi 4)

Add extended timeout configuration:

```yaml
"camera_timeout_value_ms": 1000000,
```

### Camera Calibration Files

- Camera-specific JSON files with distortion correction parameters
- Generated during the calibration process
- Located in the camera calibration directory

## System Configuration

### ActiveMQ Configuration

ActiveMQ configuration files are located in `/opt/apache-activemq/conf/`:

- `activemq.xml` - Main broker configuration
- `jetty.xml` - Web console configuration  

### TomEE Configuration

TomEE configuration files in `/opt/tomee/conf/`:

- `server.xml` - Server and context configuration
- `tomcat-users.xml` - User authentication
- `context.xml` - Application context settings

## Network Configuration

### IP Address Requirements

- **Pi 1:** Primary processing computer
- **Pi 2:** Camera computer (if using two-Pi setup)  
- **Simulator PC:** Computer running golf simulator software
- **Network:** All devices should be on the same subnet

### Port Configuration

- **ActiveMQ:** 61616 (message broker)
- **TomEE:** 8080 (web interface)
- **E6/TruGolf:** 2483 (simulator interface)

## Logging Configuration

### Log Levels

Available logging levels (in order of verbosity):
- `error`
- `warning`  
- `info`
- `debug`
- `trace`

### Usage

```bash
# Recommended for normal operation
./build/pitrac_lm --logging_level=info

# For troubleshooting
./build/pitrac_lm --logging_level=trace
```

## Troubleshooting Configuration

### Common Issues

1. **Environment variables not set:** Ensure variables are exported in shell config
2. **Wrong IP addresses:** Verify network configuration and device IPs
3. **Port conflicts:** Check that required ports are not in use by other services
4. **File permissions:** Ensure PiTrac user has access to all required directories

### Verification Commands

```bash
# Check environment variables
env | grep PITRAC

# Test network connectivity  
ping $PITRAC_MSG_BROKER_FULL_ADDRESS

# Verify file access
ls -la $PITRAC_ROOT
```

For additional configuration help, see the [Troubleshooting Guide]({% link troubleshooting.md %}).
