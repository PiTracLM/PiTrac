# PiTrac Dev Environment

So you want to hack on PiTrac? Cool. This is where the magic happens.

## The Big Picture

This isn't your typical "run these 50 commands and pray" setup. We built this thing to actually work. Every time. The whole Dev folder is basically a smart installer that knows what you need, what order to install it in, and how to fix things when they inevitably go sideways.

## Getting Started

Dead simple:
```bash
mv Dev ~/           # Put it in your home dir
cd ~/Dev           
chmod +x run.sh
./run.sh           # Launch the menu
```

That's it. Menu pops up, shows you what's installed, what's not. Pick what you want.

The typical flow:
1. **Install Software** - Gets all the dependencies
2. **System Configuration** - Sets up your Pi hardware
3. **Build PiTrac** - Actually builds the launch monitor
4. **Run PiTrac** - Launch the monitor (single or dual-Pi mode)
5. **Test Without Camera** - Test image processing without hardware
6. **Verify Installations** - Make sure it all works

## How This Thing Actually Works

### The Brain: Dependency Resolution

Everything runs through `scripts/dep_resolver.sh`. Think of it like apt-get but for PiTrac stuff. It reads `deps.conf` to figure out what needs what, then installs things in the right order. 

Say you want OpenCV? It knows you need cmake and build-essential first. Trying to install ActiveMQ C++? Better have the broker running first. It handles all that.

```bash
# See what would get installed (and in what order)
./scripts/dep_resolver.sh deps opencv
# Output: build-essential, cmake, opencv

# Actually install it
./scripts/dep_resolver.sh install opencv
```

### Circular Dependencies? We Got You

The system actively prevents dependency loops. Every install request goes through cycle detection first. Try to make package A depend on B which depends on C which depends on A? Nope:

```
[ERROR] Circular dependency detected: A -> B -> C -> A
```

How it works:
1. Builds a dependency graph
2. Tracks what's currently being resolved (the "stack")
3. If it sees something twice in the stack, that's a cycle
4. Fails fast with a clear error showing the exact loop

This runs BEFORE any installation starts, so you can't break anything.

### The Config File That Rules Them All

Check out `scripts/deps.conf`. This is where all the magic happens. Each line tells the system everything it needs to know:

```
package:dependencies:detection_method:install_script
```

Real examples:
```
# System package (installed via apt)
cmake:SYSTEM:apt:

# Package with dependencies
opencv:build-essential,cmake:function:install_opencv.sh

# Package that needs another package first
activemq-cpp:activemq-broker,libssl-dev:function:install_activemq_cpp_cms.sh
```

Detection methods:
- **apt**: Check if apt package is installed
- **cmd**: Check if command exists
- **function**: Call is_package_installed() in the script
- **pkg-config**: Use pkg-config to detect
- **file**: Check if specific file exists

### Updating Dependencies (Without Breaking Everything)

Need to change what a package depends on? Here's how:

1. **Edit deps.conf**
   ```bash
   # Before
   opencv:build-essential,cmake:function:install_opencv.sh
   
   # After (adding new dependency)
   opencv:build-essential,cmake,pkg-config:function:install_opencv.sh
   ```

2. **Test the dependency chain**
   ```bash
   ./scripts/dep_resolver.sh deps opencv
   # Shows new order including pkg-config
   ```

3. **Verify it doesn't create cycles**
   ```bash
   # The system checks automatically, but you can test:
   ./scripts/dep_resolver.sh install opencv --dry-run
   ```

4. **Update the install script if needed**
   - Detection function name must match: `is_opencv_installed()`
   - Must source common.sh
   - Must load defaults
   - Must call run_preflight_checks

### Shared Code (Because Copy-Paste is Evil)

Everything sources `scripts/common.sh`. This bad boy has all the utilities you need:

#### Sudo Handling
```bash
# Don't do this:
sudo apt-get install wget

# Do this (uses $SUDO from common.sh):
apt_ensure wget
```

#### Temp Directory Management
```bash
# Don't do this:
WORK=$(mktemp -d)
trap "rm -rf $WORK" EXIT

# Do this (auto-cleanup on exit):
WORK=$(create_temp_dir "mypackage")
cd "$WORK"
# Automatically cleaned up when script exits
```

#### Logging with Colors
```bash
log_info "Starting installation..."     # Blue [INFO]
log_success "Installation complete!"    # Green [SUCCESS]
log_warn "This might take a while..."   # Yellow [WARN]
log_error "Something went wrong"        # Red [ERROR]
```

#### Package Installation
```bash
# Only installs if not already present
apt_ensure git cmake build-essential

# Multiple packages at once
apt_ensure libgtk-3-dev libavcodec-dev libavformat-dev
```

#### Version Checking
```bash
# Compares versions intelligently
if version_ge "$current_version" "$required_version"; then
    log_success "Version OK"
fi
```

#### Download with Progress Bar
```bash
# Shows actual progress
download_with_progress "https://example.com/file.tar.gz" "file.tar.gz"
```

#### Pre-flight Checks
```bash
# Checks disk space, internet, system requirements
run_preflight_checks "opencv" || return 1
```

#### Build Progress Monitoring
```bash
# Shows compilation progress
run_with_progress "make -j$(get_cpu_cores)" "Building OpenCV" "/tmp/build.log"
```

### Non-Interactive Mode (For Automation)

Every script can run hands-free. Here's the complete flow:

1. **Create defaults file** in `scripts/defaults/package.yaml`:
   ```yaml
   # opencv.yaml
   required-opencv-version: 4.11.0
   force: 0
   build-examples: 1
   build-type: Release
   install-python-bindings: 1
   ```

2. **Script loads these automatically**:
   ```bash
   # In install script
   load_defaults "opencv" "$@"
   # Now all YAML values are environment variables
   ```

3. **Run non-interactively**:
   ```bash
   ./install_opencv.sh --non-interactive
   # or
   NON_INTERACTIVE=1 ./install_opencv.sh
   ```

4. **What changes in non-interactive mode**:
   - No prompts (uses defaults for everything)
   - Fails fast on errors (no "Continue anyway?" prompts)
   - More verbose logging
   - Returns proper exit codes for scripting

### Building PiTrac After Dependencies

Once you've got all the dependencies installed, you need to actually build PiTrac. We've got that covered too:

```bash
./run.sh
# Choose option 3: Build PiTrac
```

What this does:
1. **Clones the PiTrac repo** (or updates if you already have it)
2. **Checks out the right branch** (configurable in defaults/pitrac-build.yaml)
3. **Sets up environment variables** (or uses ones from pitrac-environment)
4. **Configures libcamera** for your specific Pi model
5. **Builds the launch monitor** with meson/ninja
6. **Optionally builds the web GUI** if TomEE is installed

The build script is smart:
- Won't let you build if dependencies are missing
- Detects Pi model and adjusts accordingly
- Uses optimal core count for compilation
- Shows progress bars for long builds
- Can resume failed builds

Want a different branch?
```yaml
# Edit scripts/defaults/pitrac-build.yaml
pitrac-branch: develop  # or feature/whatever
```

Need to rebuild from scratch?
```yaml
clean-build: 1
force-clone: 1
```

### Running PiTrac Launch Monitor

After building, you can run PiTrac from the menu:

```bash
./run.sh
# Choose option 4: Run PiTrac Launch Monitor
```

The run system supports:
- **Single-Pi mode** - Everything runs on one Pi
- **Dual-Pi mode** - Camera 1 (primary) and Camera 2 (secondary)
- **Background processes** - Run cam1 and cam2 simultaneously
- **Process management** - View logs, check status, stop processes
- **Hardware tests** - Test strobe lights and camera triggers
- **Auto-restart** - Configurable restart on failure

Configuration in `scripts/defaults/run-pitrac.yaml`:
```yaml
pi_mode: single         # or dual
logging_level: info     # trace, debug, info, warning, error
auto_restart: 0         # Enable auto-restart on failure
```

The run script:
- Checks for ActiveMQ and TomEE services (non-blocking)
- Loads environment variables from your shell config
- Falls back to RunScripts if they exist, or uses command-line params
- Manages PIDs for background processes
- Provides full process control from the menu

For dual-Pi setup, the communication happens via ActiveMQ message broker (not SSH). Start Camera 2 first, then Camera 1.

### Testing Without Camera Hardware

Don't have cameras connected? Want to test the image processing? We've got you covered:

```bash
./run.sh
# Choose option 7: Test Image Processing (No Camera)
```

This launches a test environment that:
- **Uses test images** instead of live camera feed
- **Processes teed and strobed ball images** 
- **Generates full shot data** (speed, angles, spin)
- **Creates visual outputs** (detected balls, trajectories)
- **Runs a web server** to view results

Test modes:
1. **Quick Test** - Uses default test images from TestImages/
2. **Custom Test** - Specify your own teed/strobed images
3. **Results Server** - View results in browser at localhost:8080

The test processor:
- Verifies image processing algorithms
- Tests ball detection and tracking
- Validates spin calculation
- Checks trajectory computation
- No hardware required

Perfect for:
- Development and debugging
- Algorithm verification
- CI/CD pipelines
- Learning how PiTrac works

### Writing a New Install Script

Here's the complete template for a new package:

```bash
#!/usr/bin/env bash
set -euo pipefail

# MyPackage Installation Script
SCRIPT_DIR="$(cd -- "$(dirname "$0")" >/dev/null 2>&1 && pwd)"
source "${SCRIPT_DIR}/common.sh"

# Load defaults from config file
load_defaults "mypackage" "$@"

# Set defaults (can be overridden by YAML)
REQUIRED_VERSION="${REQUIRED_VERSION:-1.0.0}"
FORCE="${FORCE:-0}"

# Detection function (MUST match package name)
is_mypackage_installed() {
    # Return 0 if installed, 1 if not
    command -v mypackage >/dev/null 2>&1
}

# Main installation function
install_mypackage() {
    # ALWAYS run preflight checks first
    run_preflight_checks "mypackage" || return 1
    
    # Check if already installed
    if is_mypackage_installed && [ "$FORCE" != "1" ]; then
        log_success "MyPackage already installed"
        return 0
    fi
    
    # Install dependencies
    apt_ensure wget build-essential
    
    # Create temp directory (auto-cleaned)
    local WORK
    WORK=$(create_temp_dir "mypackage")
    cd "$WORK"
    
    # Download with progress
    download_with_progress "https://example.com/mypackage.tar.gz" "mypackage.tar.gz"
    
    # Extract and build
    tar -xzf mypackage.tar.gz
    cd mypackage-*
    
    # Build with progress monitoring
    log_info "Configuring..."
    ./configure
    
    run_with_progress "make -j$(get_cpu_cores)" "Building MyPackage"
    
    log_info "Installing..."
    $SUDO make install
    
    # Verify installation
    if is_mypackage_installed; then
        log_success "MyPackage installed successfully"
    else
        log_error "Installation verification failed"
        return 1
    fi
}

# Run if called directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    install_mypackage
fi
```

### Adding Your Package to the System

1. **Create the install script** (following template above)

2. **Add to deps.conf**:
   ```bash
   # Simple package
   mypackage:build-essential:function:install_mypackage.sh
   
   # Package with multiple deps
   mypackage:cmake,libssl-dev,boost:function:install_mypackage.sh
   
   # System package (no script needed)
   mypackage:SYSTEM:apt:
   ```

3. **Create defaults YAML** in `scripts/defaults/mypackage.yaml`:
   ```yaml
   required-version: 1.0.0
   force: 0
   enable-feature-x: 1
   build-type: Release
   ```

4. **Test detection**:
   ```bash
   ./scripts/dep_resolver.sh verify mypackage
   ```

5. **Test installation**:
   ```bash
   ./scripts/dep_resolver.sh install mypackage
   ```

### Error Handling That Won't Ruin Your Day

The scripts are carefully designed to not mess up your shell:

#### Return vs Exit
- **Functions use `return`** - Won't kill your shell if sourced
- **Main scripts use `exit`** - Only when run directly
- **Subshells use `exit`** - Only affects the subshell

#### Error Propagation
```bash
install_mypackage() {
    run_preflight_checks "mypackage" || return 1
    do_something || return 1
    do_another_thing || return 1
}
```

#### Interactive vs Non-Interactive
```bash
if is_non_interactive; then
    # Fail immediately
    return 1
else
    # Ask user
    read -p "Continue anyway? (y/N): " -n 1 -r
    [[ $REPLY =~ ^[Yy]$ ]] || return 1
fi
```

### The Install Order (How Dependencies Actually Work)

When you run `./scripts/dep_resolver.sh install opencv`, here's what happens:

1. **Parse deps.conf** to find opencv's dependencies
2. **Recursively resolve** each dependency's dependencies
3. **Check for circular dependencies** (fail if found)
4. **Build installation order** (dependencies first)
5. **Check what's already installed** (skip those)
6. **Install in order**, checking each step
7. **Log everything** for rollback if needed

Example with activemq-cpp:
```
activemq-cpp depends on: activemq-broker, libssl-dev, libapr1-dev
activemq-broker depends on: nothing
libssl-dev depends on: SYSTEM
libapr1-dev depends on: SYSTEM

Install order:
1. libssl-dev (apt)
2. libapr1-dev (apt)
3. activemq-broker (script)
4. activemq-cpp (script)
```

### System Packages vs Custom Installs

There are two types in deps.conf:

**System Packages** (SYSTEM flag):
```
cmake:SYSTEM:apt:
```
- Installed via apt-get
- Simple version checking
- Fast installation

**Custom Packages** (with scripts):
```
opencv:build-essential,cmake:function:install_opencv.sh
```
- Complex installation logic
- Custom version detection
- Build from source
- Configuration options

### The Config Scripts (Not Just Installs)

In `scripts/config/` you'll find system configuration scripts:

**system_config.sh**
- GPU memory split
- Boot configuration
- Hardware-specific tweaks

**camera_config.sh** 
- Camera sensor detection
- Device tree overlays
- Sensor-specific configs

**network_services.sh**
- NAS mount points
- Samba shares
- SSH key management

**pitrac_environment.sh**
- Environment variables
- PATH setup
- App configuration

**dev_environment.sh**
- Optional developer tools
- ZSH + Oh-My-ZSH
- Neovim setup
- Productivity tools

These use the same patterns but configure the system rather than install packages.

### Troubleshooting Like a Pro

#### Check What Would Happen
```bash
# See dependency tree
./scripts/dep_resolver.sh deps package_name

# List all available packages
./scripts/dep_resolver.sh list
```

#### When Something Fails
```bash
# Check if it's really installed
./scripts/dep_resolver.sh verify package_name

# See the install log
cat scripts/.install.log

# Check the rollback log
cat scripts/.rollback.log
```

#### Starting Fresh
```bash
# Undo last installation
./scripts/dep_resolver.sh rollback

# Nuclear option - reset everything
rm scripts/.install.log
rm scripts/.lock
```

#### Debug Mode
```bash
# Run with bash debugging
bash -x ./scripts/install_opencv.sh

# Extra verbose
set -x
./scripts/dep_resolver.sh install opencv
```

### Making Changes Without Fear

The system is designed to be hackable:

1. **All changes are logged** - You can see what happened
2. **Dependencies are verified** - Can't create broken states
3. **Functions return errors** - Won't crash your shell
4. **Temp dirs auto-clean** - No leftover mess
5. **Versions are checked** - Won't downgrade by accident

Want to experiment? Just:
1. Copy an existing script as template
2. Modify for your needs
3. Test with verify first
4. Run the install
5. If it breaks, rollback

### Common Patterns You'll See

#### Architecture Detection
```bash
arch="$(uname -m)"
if [[ "$arch" == "aarch64" ]]; then
    # ARM/Pi specific code
else
    # x86_64 code
fi
```

#### Pi Model Detection
```bash
pi_model=$(detect_pi_model)  # Returns 4, 5, or unknown
if [ "$pi_model" = "5" ]; then
    # Pi5 specific stuff
fi
```

#### Conditional Features
```bash
if [ "${ENABLE_FEATURE:-0}" = "1" ]; then
    # Feature enabled in YAML/environment
fi
```

#### Safe Command Execution
```bash
# Don't assume commands exist
if command -v some_command >/dev/null 2>&1; then
    some_command
fi
```

## The Philosophy

We wrote this because the old way sucked. Random scripts from random tutorials that worked on some specific Pi model with some specific OS version three years ago. No more.

This system:
- Works on Pi4, Pi5, x86_64
- Handles Bookworm and Bullseye  
- Detects what you have
- Installs what you need
- Gets out of your way
- Doesn't pretend to be smarter than you

It's not perfect, but it's a hell of a lot better than copy-pasting from forum posts and hoping for the best.

## Quick Reference

### Commands You'll Actually Use
```bash
# Main menu - everything starts here
./run.sh

# Install something specific
./scripts/dep_resolver.sh install package_name

# Check if something's installed
./scripts/dep_resolver.sh verify package_name

# See what depends on what
./scripts/dep_resolver.sh deps package_name

# List everything available
./scripts/dep_resolver.sh list

# Undo last install
./scripts/dep_resolver.sh rollback

# Run PiTrac after building
./scripts/run_pitrac.sh           # Single-Pi mode
./scripts/run_pitrac.sh cam1       # Camera 1 (dual-Pi)
./scripts/run_pitrac.sh cam2       # Camera 2 (dual-Pi)

# Test without cameras
./scripts/test_image_processor.sh quick     # Quick test with defaults
./scripts/test_image_processor.sh list      # List available test images
python3 scripts/test_results_server.py      # View results in browser
```

### Files You'll Actually Edit
- `scripts/deps.conf` - Package definitions and dependencies
- `scripts/defaults/*.yaml` - Default values for each package
- `scripts/install_*.sh` - Individual install scripts
- `scripts/common.sh` - Shared utilities (probably don't touch)

### Environment Variables That Matter
- `NON_INTERACTIVE=1` - Run without prompts
- `FORCE=1` - Reinstall even if present
- `SKIP_PREFLIGHT=1` - Skip disk/network checks (dangerous)

## Contributing

Found a bug? Script could be better? Package needs updating? 

The code's all here. Make it better. Just remember:
- Test your dependency changes with the cycle detector
- Keep the patterns consistent (look at existing scripts)
- Update deps.conf if you change dependencies
- Create a defaults YAML for new packages
- Use common.sh functions instead of reinventing
- Test on both Pi and x86 if possible
- Document the weird stuff

That's it. Go build something cool.