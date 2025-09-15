---
layout: default
title: Service Architecture
parent: Development Guide
nav_order: 9
---

# Service Architecture

PiTrac uses a modern service architecture where the launch monitor processes are managed through the web UI, not systemd. Only supporting services run as systemd services.

## Service Components

### 1. PiTrac Web Server (pitrac-web.service)

The primary interface for all PiTrac operations:

```ini
[Unit]
Description=PiTrac Web Dashboard
After=network.target activemq.service
Wants=activemq.service

[Service]
Type=simple
User=@PITRAC_USER@
WorkingDirectory=/usr/lib/pitrac/web-server
Environment="PATH=/usr/bin:/bin"
ExecStart=/usr/bin/python3 /usr/lib/pitrac/web-server/main.py
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

**Management:**
```bash
# Start web server (primary command)
pitrac web start

# Check status
pitrac web status

# View logs
pitrac web logs
```

### 2. ActiveMQ Message Broker (activemq.service)

Handles inter-process communication between PiTrac components:

```ini
[Unit]
Description=Apache ActiveMQ
After=network.target

[Service]
Type=forking
User=activemq
Environment="JAVA_HOME=/usr/lib/jvm/default-java"
ExecStart=/usr/share/activemq/bin/activemq start
ExecStop=/usr/share/activemq/bin/activemq stop
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

**Note:** ActiveMQ is typically managed automatically and doesn't require manual intervention.

## Service Installation

During installation (`sudo ./build.sh dev`), services are configured:

```bash
# Web server service installation
/usr/lib/pitrac/web-service-install.sh install <username>

# ActiveMQ configuration
/usr/lib/pitrac/activemq-service-install.sh install activemq
```

## Service Dependencies

```
Network
  ↓
ActiveMQ (Message Broker)
  ↓
PiTrac Web Server
  ↓
PiTrac Launch Monitor (managed by web UI)
```

## Process Management Architecture

### Web UI Process Control

The web server manages PiTrac processes through:

1. **Process Spawning** - Uses Python subprocess to start `pitrac_lm`
2. **Health Monitoring** - Checks process status via PID files
3. **Configuration Building** - Generates CLI arguments from web UI settings
4. **Log Management** - Captures and streams process output
5. **Graceful Shutdown** - Sends appropriate signals (SIGTERM/SIGKILL)

### Process Lifecycle

```python
# Simplified process management in web server
def start_pitrac():
    # Build command from configuration
    cmd = build_pitrac_command(config)

    # Start process
    process = subprocess.Popen(cmd, ...)

    # Store PID for monitoring
    save_pid(process.pid)

    # Monitor health
    schedule_health_check()

def stop_pitrac():
    # Get PID
    pid = load_pid()

    # Graceful shutdown
    os.kill(pid, signal.SIGTERM)

    # Wait for termination
    wait_for_process_exit(pid, timeout=30)
```

## Service File Locations

- **Service Templates**: `/usr/share/pitrac/templates/`
- **Installed Services**: `/etc/systemd/system/`
- **Service Installers**: `/usr/lib/pitrac/*-service-install.sh`
- **PID Files**: `~/.pitrac/run/`
- **Log Files**: `~/.pitrac/logs/`

## Systemd Commands (For Supporting Services Only)

```bash
# Check service status
systemctl status pitrac-web
systemctl status activemq

# View service logs
journalctl -u pitrac-web -f
journalctl -u activemq -f

# Enable on boot
sudo systemctl enable pitrac-web
sudo systemctl enable activemq

# Restart services
sudo systemctl restart pitrac-web
sudo systemctl restart activemq
```

## Development Considerations

### Adding New Services

If adding a new systemd service:

1. Create template in `packaging/templates/`
2. Add installer script in `packaging/src/lib/`
3. Update `build.sh` to install service
4. Document in this guide

### Process vs Service Decision

Use a systemd service when:
- Process needs to run continuously
- Automatic startup on boot required
- No user interaction needed
- Simple start/stop semantics

Manage via web UI when:
- Dynamic configuration needed
- User control required
- Complex startup sequences
- Real-time monitoring important

## Troubleshooting Services

### Web Server Issues

```bash
# Check if running
systemctl status pitrac-web

# Check port availability
netstat -tln | grep 8080

# View detailed logs
journalctl -u pitrac-web -n 100
```

### ActiveMQ Issues

```bash
# Verify ActiveMQ is running
systemctl status activemq

# Check if listening
netstat -tln | grep 61616

# Test connection
telnet localhost 61616
```

### PiTrac Process Issues

Since PiTrac is managed by the web UI:

1. Check web UI "PiTrac Process" section for status
2. View logs in web UI "Logs" section
3. Check for PID files: `ls ~/.pitrac/run/`
4. Look for running processes: `pgrep pitrac_lm`

## Best Practices

1. **Always use web UI for PiTrac control** - Don't try to run `pitrac_lm` manually
2. **Monitor through web UI** - Real-time status and logs
3. **Let services auto-start** - Enable systemd services for boot
4. **Check dependencies** - Ensure ActiveMQ starts before web server
5. **Use proper shutdown** - Stop via web UI before system shutdown

## Migration Notes

If upgrading from older PiTrac versions:

- Old `pitrac.service` is removed during installation
- PiTrac process control moved to web UI
- TomEE replaced with Python web server
- Configuration now managed through web UI

The service architecture prioritizes user control and monitoring through the web interface while maintaining system services only for infrastructure components.