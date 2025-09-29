---
layout: default
title: Build System
parent: Development Guide
nav_order: 2
---

# PiTrac Build System

The PiTrac build system manages compilation, dependency building, and package creation. This guide documents the actual build scripts and processes.

## Quick Start

### Development Build (On Raspberry Pi)

```bash
cd packaging
sudo ./build.sh dev              # Incremental build
sudo ./build.sh dev force-rebuild # Force clean rebuild
```

**Note:** The `pitrac` CLI must be generated first using `./generate.sh` in the packaging directory.

### Package Build

```bash
cd packaging
./build.sh all            # Build dependencies then PiTrac
./build-apt-package.sh    # Create .deb package (uses env vars, not flags)
```

**Environment Variables for build-apt-package.sh:**
- `PITRAC_VERSION` - Package version (default: "1.0.0")
- `PITRAC_ARCH` - Architecture (default: "arm64")

### Docker Shell

```bash
./build.sh shell          # Interactive shell with artifacts
```

## Build System Components

The build system consists of:

1. **Dependency builders** - Individual scripts to build dependencies
2. **Main orchestrator** - `build.sh` coordinates the build process
3. **Package creator** - `build-apt-package.sh` creates Debian packages
4. **CLI generator** - `generate.sh` creates the pitrac command from bashly.yml

### Key Scripts

#### `packaging/build.sh`

Actual supported modes:

```bash
./build.sh          # Default: build (requires existing deps)
./build.sh deps     # Build dependency artifacts if missing
./build.sh build    # Build PiTrac using artifacts
./build.sh all      # Build deps then PiTrac
./build.sh dev      # Build and install directly on Pi
./build.sh clean    # Remove all artifacts and Docker images
./build.sh shell    # Interactive shell with artifacts
./build.sh help     # Show usage
```

**Force rebuild:** Add `force-rebuild` or `force` as second parameter:
```bash
./build.sh dev force-rebuild
./build.sh all force
```

#### `packaging/build-apt-package.sh`

Creates Debian packages. Uses environment variables (NOT command-line flags):

```bash
# Default usage
./build-apt-package.sh

# With custom version and architecture
PITRAC_VERSION="2.1.0" PITRAC_ARCH="arm64" ./build-apt-package.sh
```

The script:
- Extracts binaries from Docker builds
- Bundles dependencies
- Creates Debian control files
- Includes systemd services

#### `packaging/scripts/` - Dependency Builders

Verified scripts that exist:

- `build-opencv.sh` - OpenCV 4.11.0
- `build-activemq.sh` - ActiveMQ-CPP 3.9.5
- `build-lgpio.sh` - GPIO control library 0.2.2
- `build-msgpack.sh` - MessagePack C++ 6.1.1
- `build-tomee.sh` - TomEE 10.1.0
- `build-webapp.sh` - Web application
- `build-all-deps.sh` - Builds all dependencies in sequence

### Docker Infrastructure

#### Dockerfiles

Actual Dockerfiles in the project (all single-stage builds):

- `Dockerfile.opencv` - OpenCV builder
- `Dockerfile.activemq` - ActiveMQ builder
- `Dockerfile.lgpio` - lgpio builder
- `Dockerfile.msgpack` - MessagePack builder
- `Dockerfile.tomee` - TomEE builder
- `Dockerfile.pitrac` - Main PiTrac builder
- `Dockerfile.bashly` - CLI generator

All Dockerfiles use single-stage builds with:
```dockerfile
FROM --platform=linux/arm64 debian:bookworm-slim
```

### Dependency Management

#### Artifact Storage

Verified artifacts in `packaging/deps-artifacts/`:

```
deps-artifacts/
├── opencv-4.11.0-arm64.tar.gz
├── opencv-4.11.0-arm64.metadata
├── activemq-cpp-3.9.5-arm64.tar.gz
├── activemq-cpp-3.9.5-arm64.metadata
├── lgpio-0.2.2-arm64.tar.gz
├── lgpio-0.2.2-arm64.metadata
├── msgpack-cxx-6.1.1-arm64.tar.gz
├── msgpack-cxx-6.1.1-arm64.metadata
├── tomee-10.1.0-plume-arm64.tar.gz
└── tomee-10.1.0-plume-arm64.metadata
```

#### Metadata Format

Actual metadata format (plain text, not JSON):

```
OpenCV 4.11.0 ARM64 Build
Build Date: Sun Aug 17 03:52:55 UTC 2025
Architecture: aarch64
Debian Version: bookworm
c9c4e1c7ccd669e52c465448ff4d4341  /opencv-4.11.0-arm64.tar.gz
```

### Build Configuration

#### Meson Configuration

Actual `meson.build` configuration in `Software/LMSourceCode/ImageProcessing/`:

```meson
project('pitrac', 'cpp',
  default_options : [
    'werror=true',
    'warning_level=2',
    'cpp_std=c++20'
  ]
)
```

**Note:** The actual file does NOT include `optimization=3` or `b_lto=true` options.

Dependencies are correctly defined:
- opencv4 (>= 4.9.0)
- boost (system, thread modules)
- threads

#### CMake Alternative

CMake files exist in the project but have different structure than shown. Multiple CMakeLists.txt files exist for different components.

## Development Workflows

### Native Development (On Raspberry Pi)

#### Initial Setup

```bash
git clone https://github.com/pitraclm/pitrac.git
cd PiTrac/packaging

# Get pre-built artifacts
git lfs pull

# Generate CLI
./generate.sh

# First build
sudo ./build.sh dev
```

#### Development Cycle

```bash
# 1. Make changes to source code

# 2. Incremental build
sudo ./build.sh dev

# 3. Force clean rebuild when needed
sudo ./build.sh dev force-rebuild
```

**Note:** Test commands require the generated pitrac CLI. Available test scripts:
- `Software/LMSourceCode/ImageProcessing/RunScripts/runTestImages.sh`
- `Dev/scripts/test_image_processor.sh`

### Cross-Platform Development

#### Build Process

```bash
cd packaging

# Build dependencies (first time only)
./scripts/build-all-deps.sh

# Build PiTrac
./build.sh build

# Create package
./build-apt-package.sh

# Package will be in build/package/
```

### GitHub Actions

Actual workflow files in `.github/workflows/`:

- `camera-tests.yml` - Camera code testing
- `docs-ci.yml` - Documentation CI
- `docs-deploy.yml` - Documentation deployment
- `image-analysis-tests.yml` - Image processing tests
- `java_build.yml` - Java component builds
- `yaml_lint.yml` - YAML validation
- `cla.yml` - Contributor License Agreement

### Docker Development

```bash
# Interactive shell
./packaging/build.sh shell
```

Docker builds use single-stage Dockerfiles with:
```bash
docker buildx build --platform=linux/arm64 ...
```

## Build Notes

### Incremental Builds

The `build.sh dev` mode supports incremental builds on Raspberry Pi.

### Architecture Support

All Dockerfiles specify ARM64 platform:
```dockerfile
FROM --platform=linux/arm64 debian:bookworm-slim
```

## Troubleshooting

### Common Issues

#### Missing Dependencies

Build dependency artifacts first:
```bash
cd packaging
./scripts/build-all-deps.sh
```

#### CLI Not Found

Generate the CLI first:
```bash
cd packaging
./generate.sh
```

#### Force Rebuild

```bash
./build.sh dev force-rebuild
# or
./build.sh all force
```

## Additional Scripts

### Test Scripts

Available test scripts:
- `Software/LMSourceCode/ImageProcessing/RunScripts/runTestImages.sh`
- `Software/LMSourceCode/ImageProcessing/RunScripts/runTestGsProServer.sh`
- `Dev/scripts/test_image_processor.sh`

### CLI Generator

The `pitrac` command is generated from `bashly.yml`:

```bash
cd packaging
./generate.sh  # Creates pitrac-cli from bashly.yml
```

Supported commands (when generated):
- `run`, `stop`, `status`
- `setup`, `config`, `camera`
- `test` (with subcommands: hardware, pulse, quick, spin, gspro, automated, camera)
- `calibrate`, `service`, `tomee`, `activemq`
- `logs`, `boot`, `version`

## Summary

The PiTrac build system uses:
- Docker containers for reproducible builds (single-stage, ARM64)
- Pre-built dependency artifacts stored in `deps-artifacts/`
- Meson as the primary build system
- Bashly for CLI generation
- Multiple test scripts for verification

Key commands:
- `./build.sh all` - Build everything
- `./build.sh dev` - Development build on Pi
- `./build-apt-package.sh` - Create Debian package
- `./generate.sh` - Generate CLI from bashly.yml