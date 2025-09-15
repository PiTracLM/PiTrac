---
layout: default
title: System Overview
parent: Development Guide
nav_order: 1
---

# PiTrac System Overview

Understanding PiTrac's architecture is essential for effective development. This document provides a comprehensive overview of the system's components, their interactions, and the design decisions that shape the project.

## Modern Architecture

PiTrac uses a web-first architecture where all user interaction happens through a modern web interface. The system balances performance requirements with user experience through careful design choices.

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    User Interface Layer                  │
│  ┌────────────────────────────────────────────────┐     │
│  │         Web Dashboard (Port 8080)              │     │
│  │  - Configuration Management                    │     │
│  │  - Process Control (Start/Stop PiTrac)        │     │
│  │  - Real-time Shot Display                     │     │
│  │  - Testing & Calibration                      │     │
│  │  - System Monitoring & Logs                   │     │
│  └──────────────────┬─────────────────────────────┘     │
│                     │                                    │
│  ┌──────────────────▼─────────────────────────────┐     │
│  │    Python FastAPI Web Server                   │     │
│  │    (WebSocket + REST API)                      │     │
│  └──────────────────┬─────────────────────────────┘     │
└────────────────────┼────────────────────────────────────┘
                     │
┌────────────────────┼────────────────────────────────────┐
│              Service Layer                               │
│  ┌──────────────────▼─────────────────────────────┐     │
│  │          Message Broker (ActiveMQ)             │     │
│  └──────┬───────────┬──────────────┬──────────────┘     │
│         │           │              │                     │
│  ┌──────▼────┐     │      ┌───────▼───────┐           │
│  │  PiTrac   │     │      │   Simulator   │           │
│  │  Core LM  │◄────┘      │   Interface   │           │
│  │  (Managed │            │  (E6/GSPro)   │           │
│  │   by Web) │            └───────────────┘           │
│  └──────┬────┘                                         │
└─────────┼───────────────────────────────────────────────┘
          │
┌─────────▼───────────────────────────────────────────────┐
│                   Hardware Layer                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │  Camera 1   │  │  Camera 2   │  │ GPIO/Strobe │     │
│  │  (Global    │  │  (Global    │  │   Control   │     │
│  │   Shutter)  │  │   Shutter)  │  │   (lgpio)   │     │
│  └─────────────┘  └─────────────┘  └─────────────┘     │
└─────────────────────────────────────────────────────────┘
```

### Core Components

#### Web Dashboard 

The modern Python-based web interface that replaces all CLI and manual operations:

- **FastAPI Framework**: High-performance async web server
- **WebSocket Support**: Real-time updates without polling
- **Process Management**: Start/stop PiTrac processes dynamically
- **Configuration UI**: Graphical configuration with validation
- **Testing Suite**: Hardware and system tests with live feedback
- **Calibration Wizard**: Step-by-step camera calibration
- **Log Streaming**: Real-time log viewing and filtering

Key files (`Software/web-server/`):
- `server.py` - FastAPI application and routes
- `listeners.py` - ActiveMQ message handlers
- `managers.py` - Shot and session management
- `static/js/` - Frontend JavaScript (vanilla JS, no framework)
- `templates/` - Jinja2 HTML templates

#### PiTrac Launch Monitor Core (`pitrac_lm`)

- **Image Acquisition**: Interfaces with Pi cameras through libcamera
- **Ball Detection**: Real-time computer vision using OpenCV
- **Physics Calculation**: 3D trajectory computation from stereo images
- **Message Publishing**: Sends results via ActiveMQ

Key source files:
- `ball_image_proc.cpp` - Main image processing pipeline
- `gs_fsm.cpp` - Finite state machine for shot detection
- `pulse_strobe.cpp` - Hardware strobe synchronization
- `configuration_manager.cpp` - Configuration handling

#### Configuration Management

A three-tier configuration system managed through the web UI:

1. **System Defaults** - Built into the application
2. **Calibration Data** - Camera-specific calibration parameters
3. **User Overrides** - Settings changed through web UI

Configuration flow:
```
Web UI Changes → REST API → YAML Files → ConfigurationManager → pitrac_lm
```

Key files:
- `~/.pitrac/config/` - User configuration (managed by web UI)
- `configuration_manager.cpp` - C++ configuration logic
- `Software/web-server/config_manager.py` - Python configuration handler

#### Message Broker (ActiveMQ)

Provides communication between components:

- **Asynchronous Messaging** - Non-blocking communication
- **Topic-based Routing** - Publishers and subscribers
- **Shot Data Distribution** - From pitrac_lm to web UI
- **Status Updates** - System health and diagnostics

Topics:
- `Golf.Sim` - Primary message topic for shot data
- `Golf.Status` - System status messages

#### Process Architecture

Unlike traditional service architectures, PiTrac uses dynamic process management:

```python
# Web server manages PiTrac processes
class PiTracManager:
    def start_camera(self, camera_num):
        # Build command from web UI configuration
        cmd = self.build_command(camera_num)

        # Spawn process (not a service)
        process = subprocess.Popen(cmd)

        # Monitor health
        self.monitor_process(process)

    def stop_camera(self, camera_num):
        # Graceful shutdown via signals
        self.send_shutdown_signal(camera_num)
```

## Data Flow

### Shot Detection Flow

```
1. User starts PiTrac via web UI
2. Web server spawns pitrac_lm process
3. Cameras capture images at 232 fps
4. Ball detection triggers shot sequence
5. Image processing calculates metrics
6. Results published to ActiveMQ
7. Web server receives via listener
8. WebSocket broadcasts to browser
9. UI updates in real-time
```

### Configuration Update Flow

```
1. User modifies setting in web UI
2. Frontend validates input
3. REST API updates configuration
4. YAML file written to disk
5. User clicks "Restart PiTrac" if needed
6. Web server stops old process
7. New process started with updated config
```

## Development Workflow

### Making Changes

1. **Code Changes**: Edit source files
2. **Rebuild**: `sudo ./build.sh dev` in `packaging/`
3. **Web Server Auto-Updates**: Automatically uses new code
4. **Test via Web UI**: Use Testing section
5. **Monitor Logs**: View in web UI Logs section

### Key Development Areas

- **Web Interface**: `Software/web-server/` (Python/JavaScript)
- **Core Processing**: `Software/LMSourceCode/ImageProcessing/` (C++)
- **Build System**: `packaging/` (Bash/Docker)
- **Configuration**: Through web UI only

## Design Principles

### Web-First Approach

- All user interaction through web UI
- No manual configuration file editing
- Real-time feedback and monitoring

### Process Management

- Dynamic process spawning (not services)
- User-initiated control
- Graceful error handling
- Health monitoring

### Configuration Philosophy

- GUI-based configuration only
- Live validation and feedback
- No manual YAML/JSON editing
- Settings organized by category

## Technology Stack

### Frontend
- **Vanilla JavaScript** - No framework dependencies
- **WebSocket** - Real-time updates
- **Responsive CSS** - Mobile-friendly
- **Jinja2 Templates** - Server-side rendering

### Backend
- **Python 3.9+** - Web server
- **FastAPI** - Modern async framework
- **py-amqp-client** - ActiveMQ integration
- **subprocess** - Process management

### Core Processing
- **C++20** - Modern C++ features
- **OpenCV** - Computer vision
- **libcamera** - Camera interface
- **Boost** - Utilities and testing

### Infrastructure
- **ActiveMQ** - Message broker
- **systemd** - Service management
- **Meson/Ninja** - Build system

## Best Practices

1. **Always use web UI** for configuration and control
2. **Never edit config files** manually
3. **Monitor through web UI** for real-time status
4. **Test via web interface** for immediate feedback
5. **Check logs in web UI** for debugging

The modern architecture prioritizes user experience while maintaining the high-performance core processing that makes PiTrac effective.