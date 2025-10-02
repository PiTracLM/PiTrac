# PiTrac Packaging System

This directory contains the complete packaging ecosystem for PiTrac, enabling cross-platform builds, dependency management, and distribution package creation. This README is specifically for maintainers and developers working on the packaging infrastructure.

## Directory Structure

```
packaging/
├── build.sh                  # Master build orchestrator
├── build-apt-package.sh      # APT package creator
├── generate.sh              # Bashly CLI generator
├── bashly.yml               # CLI specification
├── pitrac                   # Generated CLI (do not edit directly)
├── pitrac-cli              # CLI wrapper template
│
├── scripts/                 # Dependency build scripts
│   ├── build-all-deps.sh   # Build all dependencies
│   ├── build-opencv.sh     # OpenCV 4.11.0
│   ├── build-activemq.sh   # ActiveMQ-CPP 3.9.5
│   ├── build-lgpio.sh      # lgpio 0.2.2
│   ├── build-msgpack.sh    # msgpack-cxx 6.1.1
│   ├── build-tomee.sh      # Apache TomEE 10.1.0
│   └── build-webapp.sh     # Web application builder
│
├── deps-artifacts/          # Pre-built dependency artifacts (NOTE: Git LFS not yet configured)
│   ├── *.tar.gz            # Compressed libraries
│   └── *.metadata          # Build information
│
├── src/                     # CLI command implementations
│   ├── run.sh              # Main run command
│   ├── config.sh           # Configuration management
│   ├── service.sh          # Service control
│   ├── camera.sh           # Camera utilities
│   ├── calibrate.sh        # Calibration tools
│   ├── logs.sh             # Log management
│   ├── version.sh          # Version information
│   ├── lib/                # Shared functions
│   └── test/               # Test implementations
│       ├── quick.sh
│       ├── hardware.sh
│       ├── camera.sh
│       ├── spin.sh
│       ├── gspro.sh
│       └── automated.sh
│
├── templates/               # Package templates
│   ├── pitrac.service      # SystemD service
│   ├── tomee.service       # TomEE service
│   ├── tomee-wrapper.sh    # TomEE wrapper script
│   ├── pitrac.yaml         # Default configuration
│   ├── golf_sim_config.json # Legacy config template
│   ├── postinst.sh         # Post-installation script
│   ├── prerm.sh            # Pre-removal script
│   └── config/             # Configuration templates
│
├── Dockerfile.*            # Docker build files
│   ├── Dockerfile.pitrac   # Main application
│   ├── Dockerfile.opencv   # OpenCV builder
│   ├── Dockerfile.activemq # ActiveMQ builder
│   ├── Dockerfile.lgpio    # lgpio builder (note: file is actually Dockerfile.lggio)
│   ├── Dockerfile.msgpack  # msgpack builder
│   ├── Dockerfile.tomee    # TomEE builder
│   └── Dockerfile.bashly   # Bashly CLI generator
│
└── fix-boost-exchange.patch # Source patch (not in subdirectory)
```

## Build System Architecture

### Build Flow

The packaging system follows a layered build approach:

1. **Dependency Building** → Pre-built artifacts stored in Git LFS
2. **Application Building** → Uses pre-built dependencies
3. **Package Creation** → Bundles everything into .deb
4. **Distribution** → GitHub releases, APT repo, Docker registry

### Key Scripts

#### build.sh - Master Orchestrator

This is the primary entry point for all build operations:

```bash
# Actions:
./build.sh          # Build PiTrac using existing artifacts (default)
./build.sh deps     # Build all dependency artifacts
./build.sh all      # Build dependencies then PiTrac
./build.sh dev      # Native development build (Pi only)
./build.sh shell    # Interactive Docker shell
./build.sh clean    # Remove all build artifacts
```

**Important Variables:**
- `ARTIFACT_DIR`: Location of dependency artifacts ($SCRIPT_DIR/deps-artifacts)
- `SCRIPT_DIR`: Current script directory
- `REPO_ROOT`: Repository root directory
- `DOCKER_BUILDKIT`: Enable BuildKit features

**Key Functions:**
- `check_artifacts()`: Ensures dependency artifacts exist
- `build_deps()`: Orchestrates dependency builds
- `build_pitrac()`: Builds PiTrac binary using Docker
- `build_dev()`: Native Pi development build with Meson

#### build-apt-package.sh - Package Creator

Creates Debian packages from build outputs:

```bash
# Usage:
./build-apt-package.sh                    # Auto-detect version and architecture
PITRAC_VERSION=2.1.0 ./build-apt-package.sh  # Specific version
PITRAC_ARCH=armhf ./build-apt-package.sh     # Different architecture
```

**Package Structure Created:**
```
pitrac_VERSION_ARCH/
├── DEBIAN/
│   ├── control      # Package metadata
│   ├── postinst     # Post-install script
│   ├── prerm        # Pre-removal script
│   └── conffiles    # Configuration files list
├── usr/
│   ├── bin/         # User executables (pitrac CLI)
│   ├── lib/pitrac/  # Libraries and binaries
│   └── share/pitrac/# Data files
│       ├── webapp/  # Web application files
│       ├── test-images/ # Test images
│       └── calibration/ # Calibration data
├── etc/
│   ├── pitrac/      # Configuration files
│   └── systemd/system/ # Service files
├── opt/
│   └── tomee/       # Web server
└── var/
    └── lib/pitrac/  # Runtime state directory
```

### Dependency Management

#### Pre-Built Artifacts

Dependencies are built once and stored as artifacts:

```bash
# Build all dependencies
./scripts/build-all-deps.sh

# Individual dependency builds
./scripts/build-opencv.sh
./scripts/build-activemq.sh
./scripts/build-lgpio.sh
./scripts/build-msgpack.sh
./scripts/build-tomee.sh
```

**Artifact Structure:**
- `DEPENDENCY-VERSION-ARCH.tar.gz`: Compressed binaries
- `DEPENDENCY-VERSION-ARCH.metadata`: Build information

#### Git LFS Setup (TODO)

**WARNING:** Git LFS is not currently configured. Large artifacts are stored directly in the repository.

To enable Git LFS for large artifacts:

```bash
# Initialize Git LFS (needs to be done)
git lfs install
git lfs track "deps-artifacts/*.tar.gz"
git add .gitattributes
git add deps-artifacts/*.tar.gz
git commit -m "Add dependency artifacts to LFS"
git lfs push
```

### Docker Build System

#### Multi-Platform Support

Building for different architectures:

```bash
# Enable buildx
docker buildx create --name pitrac-builder --use

# Build for ARM64 on x86
docker buildx build \
    --platform linux/arm64 \
    --tag pitrac:arm64 \
    --file Dockerfile.pitrac \
    --load .
```

#### Layer Caching

Optimizing build times:

```dockerfile
# Use BuildKit cache mounts
# syntax=docker/dockerfile:1.4
RUN --mount=type=cache,target=/var/cache/apt \
    apt-get update && apt-get install -y ...
```

## CLI System (Bashly)

### How It Works

The CLI is generated from `bashly.yml` specification:

```bash
# Install bashly (one-time)
gem install bashly

# Or use Docker
docker run --rm -v "$PWD:/app" dannyben/bashly

# Generate CLI from specification
./generate.sh

# This creates/updates the 'pitrac' script
```

### Adding New Commands

1. **Edit bashly.yml:**
```yaml
commands:
  - name: newcommand
    help: Description here
    args:
      - name: argument
        required: true
```

2. **Create implementation:**
```bash
# src/newcommand.sh
newcommand_command() {
    echo "Implementing new command"
    # Implementation here
}
```

3. **Regenerate CLI:**
```bash
./generate.sh
```

### Command Implementation Pattern

Each command follows this structure:

```bash
# src/COMMAND.sh

# Main command function
COMMAND_command() {
    # Access arguments
    local arg="${args[argname]}"
    local flag="${args[--flagname]}"
    
    # Implementation
    do_something "$arg"
}

# Subcommand function
COMMAND_SUBCOMMAND_command() {
    # Subcommand implementation
}
```

## Configuration Templates

### Template Processing

Templates are processed during package installation:

```bash
# In postinst.sh
envsubst < /usr/share/pitrac/templates/pitrac.yaml \
    > /etc/pitrac/pitrac.yaml
```

### Adding New Templates

1. Create template in `templates/`
2. Add to package build in `build-apt-package.sh`
3. Process in `postinst.sh` if needed

## Service Files

### SystemD Services

Services are installed to `/etc/systemd/system/`:

- `pitrac.service`: Main launch monitor
- `tomee.service`: Web interface
- `activemq.service`: Message broker (separate package)

### Modifying Services

1. Edit template in `templates/`
2. Rebuild package
3. Test with:
```bash
systemctl daemon-reload
systemctl restart SERVICE
```

## Making Changes

### Adding a New Dependency

1. **Create build script:**
```bash
# scripts/build-newdep.sh
#!/bin/bash
VERSION="1.0.0"
# Build commands...
tar -czf newdep-${VERSION}-$(uname -m).tar.gz ...
```

2. **Create Dockerfile:**
```dockerfile
# Dockerfile.newdep
FROM debian:bookworm AS builder
# Build stages...
```

3. **Update build.sh:**
```bash
# Add to DEPENDENCIES array
DEPENDENCIES+=("newdep")
```

4. **Build and commit artifact:**
```bash
./scripts/build-newdep.sh
git lfs track deps-artifacts/newdep-*.tar.gz
git add deps-artifacts/newdep-*
```

### Updating a Dependency

1. **Update version in script:**
```bash
# scripts/build-opencv.sh
OPENCV_VERSION="4.12.0"  # Was 4.11.0
```

2. **Rebuild dependency:**
```bash
./scripts/build-opencv.sh
```

3. **Replace artifact:**
```bash
rm deps-artifacts/opencv-4.11.0-*.tar.gz
git add deps-artifacts/opencv-4.12.0-*
```

4. **Update documentation:**
- Update version in docs
- Update CHANGELOG
- Test thoroughly

### Modifying Package Structure

1. **Edit build-apt-package.sh:**
```bash
# Add new files to package
cp new-file "${PKG_DIR}/usr/share/pitrac/"
```

2. **Update control file generation:**
```bash
# Adjust dependencies, size, etc.
```

3. **Test package:**
```bash
./build-apt-package.sh
sudo dpkg -i build/packages/*.deb
```

## Testing

### Local Testing

```bash
# Test build process
./build.sh all

# Test package creation
./build-apt-package.sh

# Test installation
sudo dpkg -i build/package/*.deb
pitrac test automated  # or 'pitrac test quick' for quick test

# Test removal
sudo apt remove pitrac
```

### CI Testing

Current GitHub Actions workflows:
- Component testing (image analysis, camera)
- Java build process
- Documentation deployment

**TODO:** Add workflows for:
- Multi-architecture package builds
- Package installation testing
- Service startup validation
- ARM64 platform testing

### Test Matrix

| Test | Purpose | Command |
|------|---------|---------|
| Build | Compilation works | `./build.sh all` |
| Package | Debian package valid | `dpkg-deb --info build/package/*.deb` |
| Install | Package installs cleanly | `sudo apt install ./pitrac_*.deb` |
| Service | Services start | `systemctl start pitrac` |
| Function | Basic operation | `pitrac test quick` |
| Upgrade | Upgrade path works | `sudo apt install ./pitrac_*_new.deb` |
| Remove | Clean uninstall | `sudo apt remove pitrac` |

## Troubleshooting

### Common Issues

#### Docker Build Failures

```bash
# Enable verbose output
DOCKER_BUILDKIT=1 BUILDKIT_PROGRESS=plain ./build.sh

# Clear cache
docker buildx prune -af

# Check platform
docker buildx inspect --bootstrap
```

#### Missing Dependencies

```bash
# Verify artifacts exist
ls -la deps-artifacts/

# Pull from Git LFS
git lfs pull

# Rebuild if missing
./scripts/build-all-deps.sh
```

#### Package Installation Errors

```bash
# Check package contents
dpkg-deb --contents pitrac*.deb

# Verify dependencies
apt-cache policy DEPENDENCY

# Force installation
sudo dpkg -i --force-depends pitrac*.deb
sudo apt-get install -f
```

## Release Process

### Creating a Release

1. **Update version:**
```bash
# Update version in:
# - bashly.yml (currently: 1.0.0)
# - Software/LMSourceCode/ImageProcessing/meson.build (currently: 0.0.1)
# - CMakeLists.txt files (various locations)
# Note: Version numbers need to be synchronized
```

2. **Build packages:**
```bash
./build.sh all
./build-apt-package.sh
```

3. **Test thoroughly:**
```bash
# Install and test on real Pi
sudo dpkg -i build/package/pitrac*.deb
pitrac test automated  # Run full test suite
```

4. **Tag release:**
```bash
git tag -a v2.1.0 -m "Release 2.1.0"
git push origin v2.1.0
```

5. **Upload to GitHub:**
- Create release from tag
- Upload .deb packages
- Upload source archives
- Write release notes

### Version Numbering

Follow semantic versioning:
- MAJOR.MINOR.PATCH (e.g., 2.1.0)
- Pre-release: 2.1.0-beta.1
- Development: 2.1.0-dev

## Best Practices

### Code Quality

- Always test changes on real hardware
- Keep scripts POSIX-compliant where possible
- Use shellcheck for bash scripts
- Document complex logic

### Dependency Management

- Pin exact versions for reproducibility
- Test dependency updates thoroughly
- Keep artifacts in Git LFS
- Document why building from source

### Package Quality

- Follow Debian packaging standards
- Test installation/upgrade/removal paths
- Include comprehensive postinst/prerm scripts
- Provide clear error messages

### Security

- Don't include sensitive data in packages
- Set appropriate file permissions
- Use systemd security features
- Validate all inputs

## Contributing

### Development Setup

```bash
# Clone with submodules
git clone --recursive https://github.com/pitraclm/pitrac
cd PiTrac/packaging

# Install development tools
sudo apt-get install -y \
    build-essential \
    docker.io \
    ruby \
    shellcheck

gem install bashly

# Enable Docker
sudo usermod -aG docker $USER
```

### Submitting Changes

1. Create feature branch
2. Make changes and test
3. Update documentation
4. Submit pull request
5. Ensure CI passes

## Support

For packaging-specific issues:
- GitHub Issues: https://github.com/pitraclm/pitrac/issues
- Discord: #packaging channel
- Documentation: docs/development/packaging.md

## License

The packaging system is part of PiTrac and follows the same license terms as the main project.