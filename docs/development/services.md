---
layout: default
title: Service Integration  
parent: Development Guide
nav_order: 9
---

# Service Integration

PiTrac can be configured to run as a systemd service for automatic startup and management. This guide covers the actual service files and configurations that exist in the PiTrac codebase.

## Available Service Files

The PiTrac codebase includes templates for two systemd service files:

### 1. PiTrac Service (pitrac.service)

Located at `packaging/templates/pitrac.service`:

```ini
[Unit]
Description=PiTrac Launch Monitor
After=network.target activemq.service tomee.service
Wants=activemq.service tomee.service

[Service]
Type=simple
User=@PITRAC_USER@  # Set during installation
Group=@PITRAC_GROUP@  # Set during installation
WorkingDirectory=@PITRAC_HOME@  # User's home directory
Environment="HOME=@PITRAC_HOME@"
Environment="USER=@PITRAC_USER@"
Environment="LD_LIBRARY_PATH=/usr/lib/pitrac"
Environment="PITRAC_BASE_IMAGE_LOGGING_DIR=@PITRAC_HOME@/LM_Shares/Images/"
Environment="PITRAC_WEBSERVER_SHARE_DIR=@PITRAC_HOME@/LM_Shares/WebShare/"
Environment="PITRAC_MSG_BROKER_FULL_ADDRESS=tcp://localhost:61616"
ExecStartPre=/bin/sleep 10
ExecStart=/usr/bin/pitrac run --foreground --system_mode=camera1
Restart=on-failure
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

### 2. TomEE Service (tomee.service)

Located at `packaging/templates/tomee.service`:

```ini
[Unit]
Description=Apache TomEE
After=network.target

[Service]
Type=forking
Environment="CATALINA_PID=/opt/tomee/temp/tomee.pid"
Environment="CATALINA_HOME=/opt/tomee"
Environment="CATALINA_BASE=/opt/tomee"
Environment="CATALINA_OPTS=-server"
Environment="JAVA_OPTS=-Djava.awt.headless=true"
ExecStart=/usr/lib/pitrac/tomee-wrapper.sh start
ExecStop=/usr/lib/pitrac/tomee-wrapper.sh stop
User=tomee
Group=tomee
UMask=0007
RestartSec=10
Restart=always

[Install]
WantedBy=multi-user.target
```

### TomEE Wrapper Script

The TomEE service uses a wrapper script (`packaging/templates/tomee-wrapper.sh`) that handles Java auto-detection:

```bash
#!/bin/bash
# TomEE startup wrapper with Java auto-detection

# Auto-detect JAVA_HOME if not set
if [ -z "$JAVA_HOME" ]; then
    # Try common locations in order of preference
    if [ -d "/usr/lib/jvm/default-java" ]; then
        export JAVA_HOME="/usr/lib/jvm/default-java"
    elif [ -d "/usr/lib/jvm/java-17-openjdk-arm64" ]; then
        export JAVA_HOME="/usr/lib/jvm/java-17-openjdk-arm64"
    elif [ -d "/usr/lib/jvm/java-11-openjdk-arm64" ]; then
        export JAVA_HOME="/usr/lib/jvm/java-11-openjdk-arm64"
    elif [ -x "/usr/bin/java" ]; then
        # Fallback: detect from java binary
        export JAVA_HOME=$(readlink -f /usr/bin/java | sed "s:/bin/java::")
    fi
fi

# Execute the requested command
case "$1" in
    start)
        exec /opt/tomee/bin/startup.sh
        ;;
    stop)
        exec /opt/tomee/bin/shutdown.sh
        ;;
    *)
        echo "Usage: $0 {start|stop}" >&2
        exit 1
        ;;
esac
```

**Note**: The service file uses placeholder values (@PITRAC_USER@, @PITRAC_HOME@, etc.) that are replaced during installation with the actual user's information. This allows the service to work with any username.

## Service Management

When installed via the APT package, the services can be managed with standard systemd commands:

```bash
# Start the PiTrac service
sudo systemctl start pitrac

# Stop the PiTrac service
sudo systemctl stop pitrac

# Check service status
systemctl status pitrac

# Enable auto-start at boot
sudo systemctl enable pitrac

# View service logs
journalctl -u pitrac -f
```

## CLI Integration

The PiTrac CLI (`packaging/pitrac`) includes service management commands:

```bash
# Service management
pitrac service start|stop|restart|status

# TomEE management
pitrac tomee start|stop|restart|status

# View logs
pitrac logs [--follow]
```

## Configuration

### Environment Variables

The PiTrac service uses these environment variables (set in the service file):

- `PITRAC_BASE_IMAGE_LOGGING_DIR` - Directory for saving captured images
- `PITRAC_WEBSERVER_SHARE_DIR` - Directory for web interface data sharing
- `PITRAC_MSG_BROKER_FULL_ADDRESS` - Message broker address (for ActiveMQ integration)
- `LD_LIBRARY_PATH` - Library path for PiTrac dependencies

### System Modes

The service starts with `--system_mode=camera1` by default. Available system modes include:

- `camera1` - Single camera operation
- `camera2` - Dual camera operation
- `test` - Test mode with sample images
- `camera1_test_standalone` - Standalone test mode
- Various calibration and testing modes

## Dependencies

### ActiveMQ Integration

While the service file references ActiveMQ, it's listed as a "Wants" dependency (not required). The codebase includes:

- ActiveMQ-CPP library build support (`packaging/src/activemq.sh`)
- Installation scripts (`Dev/scripts/install_mq_broker.sh`)
- Message broker address configuration

Note: ActiveMQ is used for the PiTrac Open Interface (POI) for simulator communication but is not required for basic operation.

### TomEE Web Server

TomEE provides a web interface for monitoring. The codebase includes:

- TomEE installation scripts (`Dev/scripts/install_tomee.sh`)
- Web application archive (`golfsim-1.0.0-noarch.war`)
- JSP dashboard (`golfsim_tomee_webapp/src/main/webapp/WEB-INF/gs_dashboard.jsp`)

## Actual vs Documentation

**Important**: This documentation reflects what actually exists in the codebase. Some features mentioned in other documentation may be planned or aspirational:

- No `activemq.service` file exists in the repository
- No health check scripts (`pitrac-health-check`, `pitrac-check-deps`) exist
- No recovery scripts (`pitrac-recovery`) exist
- No Prometheus/Grafana monitoring integration exists
- No MQTT/Paho integration exists
- The service integration is simpler than depicted in some documentation

## Installation

When using the APT package installation:

1. Service files are installed to `/etc/systemd/system/`
2. The PiTrac binary is installed to `/usr/lib/pitrac/pitrac_lm`
3. The CLI wrapper is installed to `/usr/bin/pitrac`
4. Configuration files are placed in `/etc/pitrac/`

## Summary

PiTrac's service integration provides basic systemd management for the launch monitor and optional TomEE web server. The implementation is straightforward and focused on essential functionality rather than complex orchestration or monitoring features.