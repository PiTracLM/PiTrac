---
layout: default
title: Development Guide
nav_order: 10
has_children: true
permalink: /development/
---

# PiTrac Development Guide

Welcome to the PiTrac development documentation. This guide covers developing, building, packaging, and maintaining PiTrac.

## Build System Features

### Build Scripts

**Primary Build Script (`packaging/build.sh`)**
- Supports multiple build modes: `deps`, `build`, `all`, `dev`, `shell`, `clean`
- Cross-compilation for ARM64 using Docker and QEMU
- Native development mode (`dev`) for Raspberry Pi
- Artifacts-based dependency management

**APT Package Builder (`packaging/build-apt-package.sh`)**
- Creates complete Debian packages
- Bundles all required dependencies
- Generates proper control files
- Installs binaries, configs, and test resources

**CLI Generator (`packaging/generate.sh`)**
- Generates `pitrac` CLI using Bashly
- Docker-based or local Ruby generation

### Configuration System

**Hierarchical Priority (lowest to highest)**
1. Default values in `golf_sim_config.json`
2. Environment variables
3. YAML configuration (`pitrac.yaml`)
4. CLI flags

**Configuration Files**
- `/etc/pitrac/golf_sim_config.json` - JSON configuration
- `/etc/pitrac/pitrac.yaml` - YAML overrides
- `packaging/templates/pitrac.yaml` - Template file
- `packaging/parameter-mappings.yaml` - Parameter mapping definitions

### CLI Interface

**Bashly-powered `pitrac` Command**
Verified subcommands include:
- `run`, `stop`, `status` - Core operations
- `test hardware`, `test quick`, `test camera`, `test pulse`, `test spin`, `test gspro`, `test automated`
- `config`, `calibrate`, `camera` - Configuration
- `service`, `tomee`, `activemq` - Service management
- `logs`, `boot`, `version` - System utilities

## Documentation Structure

The following documentation pages are available:

**[Overview]({% link development/overview.md %})** - Architecture and system components  
**[Build System]({% link development/build-system.md %})** - Building from source  
**[Configuration Management]({% link development/configuration.md %})** - Configuration system details  
**[Packaging Guide]({% link development/packaging.md %})** - Creating APT packages  
**[CLI Interface]({% link development/cli-interface.md %})** - `pitrac` command reference  
**[Testing Framework]({% link development/testing.md %})** - Test suite documentation  
**[Dependencies Management]({% link development/dependencies.md %})** - Library management  
**[Docker Development]({% link development/docker.md %})** - Docker build environment  
**[Service Integration]({% link development/services.md %})** - SystemD and service management

## Quick Start for Developers

### Prerequisites

- Raspberry Pi 4/5 with 8GB RAM (for native development)
- Ubuntu/Debian Linux (for cross-compilation)
- Docker installed (for containerized builds)
- Git
- Basic familiarity with C++, Bash, and Linux systems

### Getting Started

1. **Clone the repository:**
   ```bash
   git clone --recursive https://github.com/pitraclm/pitrac.git
   cd PiTrac
   ```

2. **Choose your development path:**

   **For Raspberry Pi development:**
   ```bash
   cd packaging
   sudo ./build.sh dev  # Build and install locally
   pitrac test quick    # Verify installation
   ```

   **For cross-platform development:**
   ```bash
   cd packaging
   ./build.sh all              # Build everything
   ./build-apt-package.sh      # Create installable package
   ```

3. **Make changes and test:**
   ```bash
   # Edit source files in Software/LMSourceCode/ImageProcessing/
   sudo ./build.sh dev         # Incremental rebuild
   pitrac test hardware        # Test your changes
   ```

## Development Workflow

### 1. Fork and Setup
- Fork the repository on GitHub
- Clone your fork with `--recursive` flag:
  ```bash
  git clone --recursive https://github.com/YOUR_USERNAME/PiTrac.git
  cd PiTrac
  ```
- Add upstream remote:
  ```bash
  git remote add upstream https://github.com/pitraclm/pitrac.git
  ```

### 2. Development
- Sync with upstream main:
  ```bash
  git fetch upstream
  git checkout main
  git merge upstream/main
  ```
- Create feature branch:
  ```bash
  git checkout -b feature/your-feature-name
  ```
- Follow existing code patterns
- Update tests as needed

### 3. Testing
- Run `pitrac test quick` for basic validation
- Run `pitrac test hardware` on actual Pi
- Verify builds with `./build.sh build`

### 4. Submission
- Push to your fork:
  ```bash
  git push origin feature/your-feature-name
  ```
- Create pull request from your fork to upstream `main`
- Ensure CI tests pass
- Address review feedback
- Maintainers will merge when approved

## Key Components

### Dependencies (verified from meson.build)
- **OpenCV**: >= 4.9.0 (built from source, Debian has 4.6.0)
- **Boost**: >= 1.74.0 (system package)
- **ActiveMQ-CPP**: 3.9.5 (built from source)
- **libcamera**: System package
- **lgpio**: Built from source (not in repos)
- **fmt, msgpack-cxx, yaml-cpp**: System packages

### Project Structure
```
PiTrac/
├── Software/LMSourceCode/ImageProcessing/  # Core C++ code
├── packaging/                              # Build scripts and packaging
│   ├── build.sh                           # Main build script
│   ├── build-apt-package.sh               # APT package creator
│   ├── generate.sh                        # CLI generator
│   └── templates/                         # Config templates
├── docs/development/                       # This documentation
└── Hardware/                               # 3D models and PCB designs
```

## Getting Help

- **GitHub Issues**: https://github.com/pitraclm/pitrac/issues
- **Discord Community**: https://discord.gg/j9YWCMFVHN
- **Documentation**: This guide and source code

### Common Issues
- **Camera not detected**: Check `/boot/config.txt` (Pi4) or `/boot/firmware/config.txt` (Pi5) for `camera_auto_detect=1`
- **Build failures**: Run `./scripts/build-all-deps.sh` first to build dependencies
- **Missing lgpio**: Must be built from source as it's not in Debian repos

## Contributing

Before submitting:
1. Test on actual Raspberry Pi hardware
2. Follow existing code patterns
3. Update relevant documentation
4. Keep commits focused

## Next Steps

- Review [Overview]({% link development/overview.md %}) for system architecture
- See [Build System]({% link development/build-system.md %}) for build details
- Check [Packaging Guide]({% link development/packaging.md %}) for APT package creation