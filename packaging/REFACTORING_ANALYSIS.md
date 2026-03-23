# PiTrac Installation Refactoring Analysis
## COMPREHENSIVE UNDERSTANDING DOCUMENT

## 1. Current Architecture Understanding

### 1.1 Three Distinct Paths
1. **Docker Build Path** (`build.sh` without dev)
   - Runs in Docker container (ARM64 emulation on x86)
   - Builds binary for packaging
   - NO system installation
   - Produces: `pitrac_lm` binary only

2. **Developer Path** (`build.sh dev`)
   - Runs directly on Pi hardware
   - Requires sudo
   - Installs to system for immediate testing
   - Uses pre-built artifacts from git

3. **Package Path** (`build-apt-package.sh` + `postinst`)
   - Creates .deb package
   - Uses Docker-built binary
   - postinst runs on end-user system
   - Must work without Docker

### 1.2 Service Installation Systems

#### **PiTrac Service** (`service-install.sh`)
- Template: `/usr/share/pitrac/templates/pitrac.service.template`
- Variables: `@PITRAC_USER@`, `@PITRAC_GROUP@`, `@PITRAC_HOME@`
- Creates: `/etc/systemd/system/pitrac.service`
- Features:
  - User detection and validation
  - Backup of existing service
  - Directory creation for user
  - Health verification

#### **ActiveMQ Service** (`activemq-service-install.sh`)
- Templates:
  - `activemq.xml.template`
  - `log4j2.properties.template`
  - `activemq-options.template`
- Creates configs in: `/etc/activemq/instances-available/main`
- Features:
  - Template variable substitution
  - Configuration validation
  - Service verification
  - Backup mechanism

#### **Web Server Service** (`pitrac-web.service`)
- Direct service file (not template)
- Installed to: `/etc/systemd/system/pitrac-web.service`
- Override mechanism for user configuration
- Python-based, requires pip dependencies

### 1.3 Template Systems Currently in Use

#### **Service Templates**
```
templates/
├── pitrac.service.template         # Main service
├── activemq.xml.template          # ActiveMQ broker config
├── log4j2.properties.template     # ActiveMQ logging
├── activemq-options.template      # ActiveMQ JVM options
└── (NO web server template - uses static service file)
```

#### **Configuration Templates**
```
templates/
├── pitrac.yaml                    # Main config (copied as-is)
├── golf_sim_config.json          # Simulator config (copied as-is)
└── config/
    ├── settings-basic.yaml        # CLI configuration
    ├── settings-advanced.yaml     # CLI configuration
    ├── parameter-mappings.yaml    # CLI configuration
    └── README.md
```

### 1.4 Docker Compatibility Requirements

#### **build.sh (Docker mode)**
- Uses `Dockerfile.pitrac`
- Mounts repository as `/build` in container
- Runs as current user (uid:gid mapping)
- Extracts artifacts from `deps-artifacts/`
- NO system modifications allowed
- Output: Binary in build directory

#### **build-apt-package.sh**
- Can use existing binary OR extract from Docker
- Bundles all dependencies into .deb
- Must not require Docker at install time
- Creates self-contained package

## 2. Critical Issues Found

### 2.1 Service Installation Patterns
- ✅ **PiTrac service**: Uses proper template system via `service-install.sh`
- ✅ **ActiveMQ**: Uses proper template system via `activemq-service-install.sh`
- ❌ **Web server**: NO template system, direct file copy

### 2.2 Duplication Mapping

#### **Library Extraction** (Lines of duplicate code: ~60)
- `build.sh dev`: Lines 315-376
- `build-apt-package.sh`: Lines 219-260
- **Difference**: Dev extracts to system, package bundles in .deb

#### **libcamera Configuration** (Lines of duplicate code: ~25)
- `build.sh dev`: Lines 380-398
- `postinst`: Lines 98-123
- **100% identical logic**

#### **Boost C++20 Fix** (Lines of duplicate code: 4)
- `build.sh dev`: Line 442
- `postinst`: Lines 66-69
- **100% identical**

#### **pkg-config Files** (Lines of duplicate code: ~35)
- `build.sh dev`: Lines 402-433
- `postinst`: NOT PRESENT ⚠️
- **MISSING from package path - BUG!**

#### **Web Server Installation** (Lines of duplicate code: ~60)
- `build.sh dev`: Lines 616-674
- `build-apt-package.sh`: Lines 247-256
- `postinst`: Lines 58-63
- **Different approaches, same goal**

#### **Test Resources** (Lines of duplicate code: ~40)
- `build.sh dev`: Lines 499-534
- `build-apt-package.sh`: Lines 163-217
- **Nearly identical**

#### **Camera Tools** (Lines of duplicate code: ~15)
- `build.sh dev`: Lines 491-497
- `build-apt-package.sh`: Lines 190-204
- **Nearly identical**

#### **Config Files** (Lines of duplicate code: ~30)
- `build.sh dev`: Lines 536-549
- `build-apt-package.sh`: Lines 263-313
- `postinst`: Lines 39-44
- **Similar but context-specific**

#### **ActiveMQ Setup** (Lines of duplicate code: ~45)
- `build.sh dev`: Lines 551-593
- `postinst`: Lines 130-165
- **Both use activemq-service-install.sh but differently**

## 3. What Must Be Preserved

### 3.1 Docker Isolation
- build.sh (non-dev) MUST work in Docker
- Cannot make system changes in Docker mode
- Must respect uid:gid mapping
- Must use mounted volumes correctly

### 3.2 Service Installation Tools
- MUST use `service-install.sh` for pitrac service
- MUST use `activemq-service-install.sh` for ActiveMQ
- Should create template for web server (missing!)

### 3.3 Context Separation
- Dev mode: Direct installation for testing
- Package mode: Bundle for distribution
- Install mode: Configure on end-user system

## 4. Safe Refactoring Plan

### 4.1 Phase 1: Create Common Functions (LOW RISK)
Create `pitrac-common-functions.sh` with:
- `apply_boost_cxx20_fix()` - Simple sed command
- `configure_libcamera()` - Standalone logic
- `detect_pi_model()` - Already exists in postinst
- `create_pkgconfig_files()` - Currently missing from package!

### 4.2 Phase 2: Use Existing Service Tools (ZERO RISK)
- Continue using `service-install.sh` as-is
- Continue using `activemq-service-install.sh` as-is
- Create `web-service-install.sh` following same pattern

### 4.3 Phase 3: Context-Aware Functions (MEDIUM RISK)
Functions that behave differently based on context:
- `extract_libraries()` - Different targets for dev/package
- `install_test_resources()` - Different permissions
- `install_web_server()` - Different service handling

### 4.4 What NOT to Touch
- ❌ Don't change Docker build process
- ❌ Don't modify existing service installers
- ❌ Don't break uid:gid mapping
- ❌ Don't assume sudo in package context

## 5. Implementation Strategy

### 5.1 Start Small
1. Extract ONLY the 100% identical functions first
2. Test each extraction thoroughly
3. Keep original code as fallback

### 5.2 Maintain Compatibility
```bash
# Each function should check context
if [[ "${PITRAC_CONTEXT:-}" == "DOCKER" ]]; then
    # Docker-safe operations only
elif [[ "${PITRAC_CONTEXT:-}" == "DEV" ]]; then
    # System modifications allowed
elif [[ "${PITRAC_CONTEXT:-}" == "PACKAGE" ]]; then
    # Package building operations
elif [[ "${PITRAC_CONTEXT:-}" == "INSTALL" ]]; then
    # End-user installation
fi
```

### 5.3 Testing Requirements
- Test `build.sh` in Docker on x86
- Test `build.sh dev` on real Pi
- Test package creation
- Test package installation on clean Pi
- Verify no regressions

## 6. Specific Bugs to Fix

### 6.1 Missing pkg-config Files (CRITICAL)
**Problem**: Dev mode creates them, package doesn't include them
**Solution**: Add to postinst or bundle in package

### 6.2 Web Server Service Template (MEDIUM)
**Problem**: No template system for web server
**Solution**: Create web-service-install.sh following existing patterns

### 6.3 Duplicate User Detection (LOW)
**Problem**: Different user detection logic in each script
**Solution**: Standardize with get_install_user() function

## 7. Critical Docker Observations

### 7.1 Dockerfile.pitrac Creates pkg-config Files!
**Lines 86-100**: Docker creates lgpio.pc and msgpack-cxx.pc
**Issue**: These are NOT created in postinst - only in Docker and dev mode
**Impact**: This is why builds work in Docker but might fail for users

### 7.2 Boost Fix Applied in Multiple Places
- **Dockerfile.pitrac line 68**: Applied in Docker
- **build.sh dev line 442**: Applied on Pi
- **postinst lines 66-69**: Applied at install
**Note**: This is intentional - each context needs it

### 7.3 Web Server Service Differences
- **Static service file**: Uses `DynamicUser=yes`
- **Override in dev/postinst**: Changes to actual user
- **No template**: Unlike pitrac.service which uses template

## 8. Service Installation Patterns Summary

### 8.1 PiTrac Main Service
- **Template**: `pitrac.service.template`
- **Installer**: `service-install.sh`
- **Variables**: User, Group, Home directory
- **Status**: ✅ Well implemented

### 8.2 ActiveMQ Service
- **Templates**: XML, log4j2, options
- **Installer**: `activemq-service-install.sh`
- **Variables**: Broker config, ports, passwords
- **Status**: ✅ Recently refactored and working

### 8.3 Web Server Service
- **Template**: NONE (static file)
- **Installer**: NONE (direct copy)
- **Override**: SystemD drop-in file
- **Status**: ❌ NEEDS TEMPLATE SYSTEM
- **Action**: Create `web-service-install.sh` and `pitrac-web.service.template`

## 9. Safe Minimal Refactoring Plan

### Phase 1: Extract 100% Safe Functions
Create `pitrac-common-functions.sh` with ONLY:
1. `detect_pi_model()` - Already duplicated identically
2. `apply_boost_cxx20_fix()` - One-liner, safe to extract
3. `configure_libcamera()` - Standalone, no dependencies
4. `create_pkgconfig_files()` - Critical missing piece

### Phase 2: Fix Critical Bugs
1. Add pkg-config creation to postinst
2. Ensure all contexts have same pkg-config files

### Phase 3: Create Web Service Template System
1. Create `pitrac-web.service.template` with proper variables
2. Create `web-service-install.sh` following activemq pattern
3. Update all three paths to use new installer
4. Remove direct service file copies

### Phase 4: DO NOT TOUCH
- ❌ Docker build process (working fine)
- ❌ Library extraction (context-specific by design, intentional)

## 10. Next Steps

1. ✅ Understand all systems (THIS DOCUMENT)
2. ⏳ Create MINIMAL common functions (4 functions only)
3. ⏳ Fix pkg-config bug in postinst
4. ⏳ Test thoroughly before expanding
5. ⏳ Consider web service template (separate task)