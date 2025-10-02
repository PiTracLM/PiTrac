# Camera Bounded Context - Migration Guide

## Overview
Migrate from monolithic `GolfSimCamera` to a clean Camera bounded context architecture.

### Current Status
✅ **Foundation exists**: Domain objects, infrastructure abstractions, tests, and build system are ready  
🔄 **Next step**: Namespace alignment and legacy integration

### Key Decision
**Use `golf_sim::camera` namespace** - aligns with existing codebase patterns rather than creating inconsistency.

## Legacy Codebase Issues

### Naming Inconsistencies
The legacy code mixes multiple naming conventions, **with patterns related to access levels**:

**Public vs Private Patterns Discovered:**
- **Public Constants**: Consistently use `kConstantCase` (good pattern)
- **Public Methods**: Mixed `camelCase` (`getNextFrame()`) and `PascalCase` (`GetCalibratedBall()`)  
- **Private Methods**: Mostly `camelCase` (`getExpectedBallRadiusPixels()`, `getBallDistance()`)
- **Variables**: `camera_number_` (snake_case_) and `exposureTime` (camelCase) mixed together
- **Namespaces**: Uses `golf_sim::camera::domain` aligned with existing `golf_sim` pattern

**Root Cause**: The inconsistency appears to follow **intended conventions** where:
- Private methods tend toward `camelCase` 
- Public methods mix both conventions inconsistently
- Constants are consistently `kConstantCase`

### Strategic Decision: Use `golf_sim::camera`
- **Why**: `golf_sim` represents the domain function, not the brand
- **Benefit**: No massive refactoring needed across codebase
- **Consistency**: 99% of existing code already uses `golf_sim` namespace

## Migration Strategy

### Phase 0: Foundation Assessment (COMPLETED ✅)
- ✅ **Complete**: Domain value objects exist (`Size`, `Transform`, `PixelFormat`, `ColorSpace`)
- ✅ **Complete**: Infrastructure abstraction layer established
- ✅ **Complete**: Test framework with Boost Test configured  
- ✅ **Complete**: CMake build system ready
- ✅ **Complete**: Namespace alignment from `libcamera_domain` to `golf_sim::camera::domain`
- 🔄 **Next**: Integration with legacy `GolfSimCamera` class

### Phase 1: Namespace Alignment and Integration Preparation (COMPLETED ✅)

#### Step 1: Update Existing Domain Namespace ✅
```cpp
// COMPLETED (in domain/camera_domain.hpp)
namespace golf_sim::camera::domain {
    struct Size { /*...*/ };
    struct Transform { /*...*/ };
    // ...
}

// VALIDATION: All tests pass (25 test cases)
namespace golf_sim::camera::domain {
    struct Size { /*...*/ };
    struct Transform { /*...*/ };
    // ...
}
```

#### Step 2: Extend Domain with Business Logic
Add camera entities and use cases to existing domain:
```cpp
namespace golf_sim::camera::domain {
    // Existing value objects...
    struct Size { /*...*/ };
    
    // NEW: Business entities
    class CameraEntity {
        // Camera state and behavior
    };
    
    // NEW: Domain services
    class CameraCalibrationService {
        // Calibration algorithms
    };
}

namespace golf_sim::camera::application {
    // NEW: Use cases and commands
    class CaptureImageUseCase { /*...*/ };
    class CalibrateCommand { /*...*/ };
}
```

#### Step 3: Create Legacy Integration Layer
```cpp
namespace golf_sim::camera::integration {
    class LegacyCameraAdapter {
        // Bridge to existing GolfSimCamera
    };
}
```

### Phase 2: Incremental Migration (RECOMMENDED NEXT STEPS)

#### Step 1: Update IPC Integration
Replace the camera usage in `gs_ipc_system.cpp`:

```cpp
// OLD (in gs_ipc_system.cpp)
#include "gs_camera.h"
GolfSimCamera* camera = GetCameraInstance();
int result = camera->TakeStillPicture();

// NEW
#include "ImageProcessing/Camera/integration/CameraContext.h"
auto cameraContext = golf_sim::camera::integration::CameraContext::Create();
auto adapter = std::make_unique<golf_sim::camera::integration::LegacyCameraAdapter>(cameraContext);
int result = adapter->TakeStillPicture();
```

#### Step 2: Update FSM Event Handling
Replace camera calls in `gs_fsm.cpp`:

```cpp
// OLD
void ProcessCameraEvent(const CameraEvent& event) {
    auto camera = GetGolfSimCamera();
    camera->TakeStillPicture();
}

// NEW
void ProcessCameraEvent(const CameraEvent& event) {
    // Use command pattern
    using namespace golf_sim::camera::application;
    commands::CaptureImageCommand command(
        event.exposureTime,
        event.gain,
        event.resolution
    );
    cameraContext_->ExecuteCommand(command);
}
```

#### Step 3: Replace Direct Hardware Calls
Update any direct hardware access:

```cpp
// OLD (scattered throughout codebase)
#ifdef __unix__
    // Unix-specific camera code
#else
    // Windows fallback
#endif

// NEW (all handled by factory)
auto cameraContext = CameraContext::Create();  // Automatically selects platform
```

### Phase 3: Event-Driven Integration

#### Subscribe to Camera Events
```cpp
// In main application setup
auto eventBus = cameraContext->GetEventBus();

// Subscribe to image capture events
eventBus->Subscribe<Application::Events::ImageCapturedEvent>(
    [](const auto& event) {
        // Process captured image
        const auto& result = event.GetResult();
        if (result.IsSuccess()) {
            // Send to ball tracking system
            ProcessBallImage(result.GetImageData());
        }
    }
);

// Subscribe to error events
eventBus->Subscribe<Application::Events::CameraErrorEvent>(
    [](const auto& event) {
        // Log error and notify monitoring system
        LogError("Camera error: " + event.GetError());
        NotifyMonitoringSystem(event.GetError());
    }
);
```

### Phase 4: Remove Legacy Code

#### Files to Remove After Migration:
1. Remove conditional compilation from business logic
2. Clean up `gs_camera.cpp/.h` (keep only what's needed for other bounded contexts)
3. Remove platform-specific `#ifdef` blocks
4. Consolidate hardware interfaces

#### Dependencies to Update:
1. Update CMakeLists.txt to include new Camera module
2. Update include paths
3. Remove deprecated header includes

## Integration Examples

### Example 1: Basic Camera Usage
```cpp
#include "ImageProcessing/Camera/integration/CameraContext.h"

// Create camera context
auto cameraContext = golf_sim::camera::integration::CameraContext::Create();

// Configure settings
golf_sim::camera::domain::CameraSettings settings;
settings.exposureTime = 0.001;  // 1ms
settings.gain = 2.0;
settings.resolution = {1920, 1080};
cameraContext->UpdateSettings(settings);

// Capture image
golf_sim::camera::domain::CaptureSettings captureSettings{
    settings.exposureTime,
    settings.gain,
    settings.resolution,
    golf_sim::camera::domain::TriggerMode::SOFTWARE
};

auto result = cameraContext->TakeStillPicture(captureSettings);
if (result.IsSuccess()) {
    // Process image data
    ProcessImageData(result.GetImageData());
}
```

### Example 2: Event-Driven Processing
```cpp
// Set up event handling
auto eventBus = cameraContext->GetEventBus();

// Handle image capture completion
eventBus->Subscribe<golf_sim::camera::application::events::ImageCapturedEvent>(
    [&ballTracker](const auto& event) {
        const auto& result = event.GetResult();
        if (result.IsSuccess()) {
            // Forward to ball tracking bounded context
            ballTracker.ProcessImage(result.GetImageData());
        }
    }
);

// Handle camera errors
eventBus->Subscribe<golf_sim::camera::application::events::CameraErrorEvent>(
    [&logger](const auto& event) {
        logger.LogError("Camera failure: " + event.GetError());
        // Trigger recovery procedures
    }
);
```

### Example 3: Command Pattern Usage
```cpp
using namespace golf_sim::camera::application;

// Create capture command
commands::CaptureImageCommand captureCmd(
    0.001,  // exposure time
    2.0,    // gain
    {1920, 1080},  // resolution
    golf_sim::camera::domain::TriggerMode::EXTERNAL
);

// Execute command
cameraContext->ExecuteCommand(captureCmd);

// Create calibration command
commands::CalibrateCommand calibrateCmd(
    commands::CalibrateCommand::CalibrationType::FULL
);

cameraContext->ExecuteCommand(calibrateCmd);
```

## Testing Strategy

### Unit Tests
- Test each bounded context component in isolation
- Mock hardware interfaces for reliable testing
- Test command/query handlers independently

### Integration Tests
- Test camera context with real hardware
- Test event publishing/subscription
- Test legacy adapter compatibility

### Migration Tests
- Run existing system alongside new system
- Compare outputs to ensure functional equivalence
- Performance benchmarking

## Benefits After Migration

### 1. Platform Independence
- No more `#ifdef __unix__` in business logic
- Clean separation of platform-specific code
- Easier to add new platforms (ARM, different Linux distros, etc.)

### 2. Testability
- Mock hardware interfaces for unit testing
- Test business logic without hardware dependencies
- Faster test execution

### 3. Maintainability
- Single Responsibility Principle enforced
- Clear separation of concerns
- Easier to understand and modify

### 4. Extensibility
- Easy to add new camera features
- Plugin architecture for different camera types
- Event-driven architecture enables loose coupling

### 5. Performance
- Reduced conditional compilation overhead
- Better compiler optimizations
- More efficient resource usage

## TODO LIST AND REFACTORING STRATEGY

### Complexity Identified During Investigation

#### 1. Namespace Architecture Issues
**Problem**: Should we follow existing `golf_sim` namespace convention or modernize to brand-specific namespaces?

**Revised Solution Strategy** (Domain-Focused Approach):
- **Embrace `golf_sim::camera` namespace** for new bounded context (domain-aligned, not brand-aligned)
- **Rationale**: 
  - `golf_sim` represents the **system domain** (golf simulation) not the brand
  - Brand names can change (PiTrac today, could be different tomorrow)
  - Domain functions are more stable than product branding
  - Minimal disruption to existing codebase patterns
  - Follows established conventions throughout the codebase
- **Implementation**: All new Camera bounded context code uses `golf_sim::camera` namespace
- **Legacy Integration**: No namespace translation needed - seamless integration

#### 2. Naming Convention Standards for New Code
**Decision**: Establish consistent naming standards to address legacy inconsistencies

**Adopted Standards** (based on analysis of legacy codebase patterns):
```cpp
// Classes: PascalCase
class CameraEntity { };
class CaptureCommand { };
class HardwareCameraInterface { };

// Methods: PascalCase (following existing Format(), Configure() pattern)
class CameraService {
public:
    void CaptureImage();           // NOT captureImage()
    std::string Format() const;    // Consistent with legacy
    void Configure();              // Consistent with legacy
};

// Member Variables: snake_case_ (following existing member pattern)
class Camera {
private:
    double exposure_time_;         // Consistent with existing
    int camera_number_;           // Consistent with existing
    std::string device_path_;     // Consistent with existing
};

// Constants: kConstantCase (following existing kCamera1, kTest pattern)
namespace Constants {
    static constexpr double kDefaultExposureTime = 0.001;
    static constexpr int kMaxRetryAttempts = 3;
    static constexpr std::string_view kDefaultDevice = "/dev/video0";
}

// Enums: PascalCase with scoped values
enum class TriggerMode {
    Software,    // NOT SOFTWARE or kSoftware
    Hardware,
    External
};

// Namespaces: Domain-aligned hierarchical (following existing patterns)
namespace golf_sim::camera::domain { }
namespace golf_sim::camera::application { }
namespace golf_sim::camera::infrastructure { }
```

**Integration with Legacy Code**:
```cpp
// Seamless integration - no namespace translation needed
namespace golf_sim::camera {
    class CameraService {
        // New code uses consistent naming within established namespace
        void CaptureImage() {
            // Direct integration with legacy code in same namespace
            legacy_camera_->takeStillPicture();  // camelCase legacy method
        }
        
    private:
        golf_sim::GolfSimCamera* legacy_camera_;  // Same namespace
    };
}
```

#### 3. Interface Design Philosophy  
**Decision Needed**: How to handle domain vs application interfaces
- Option A: Single interface serving both domain and application needs
- Option B: Separate domain and application interfaces with adapters
- **Recommendation**: Option A for simplicity in this context

#### 4. Error Handling Strategy
**Decision Needed**: How to propagate errors through layers
- Option A: Exceptions throughout
- Option B: Result<T, Error> pattern  
- Option C: Error codes and error callbacks
- **Recommendation**: Option A for consistency with existing code

#### 5. Threading Model
**Critical Performance Requirement**: Spin rate detection is performance-critical
- **Current Performance**: ~2 seconds, optimized for multi-core processing
- **High Impact**: Improving this performance would be very valuable
- **Risk**: Degrading this performance would be undesirable

**Strategic Threading Approach**:
- **Preserve**: Existing multi-core spin analysis (don't break what works)
- **Enhance**: Add async camera capture to improve overall system responsiveness
- **Avoid**: Any threading changes that could slow down the core spin detection

**Threading Strategy**:
- **Option A**: Synchronous operations with multi-threaded image processing
- **Option B**: Async capture with parallel spin analysis pipelines  
- **Option C**: Full async/await with thread pools for compute-heavy operations
- **Recommendation**: Option B - Preserve multi-core spin processing, add async capture

### IMMEDIATE NEXT STEPS

#### Critical: Hardware-Independent Testing Strategy
**Open Source Success Requirement**: Automated testing without external hardware dependencies is **critical** for:
- **PR Validation**: Confidence that pull requests don't break existing functionality
- **Release Quality**: Assurance that code can be safely released to end users
- **Contributor Onboarding**: New developers can run tests without specialized hardware
- **CI/CD Pipeline**: Automated builds and tests in cloud environments

**Testing Architecture Requirements**:
```cpp
// Hardware abstraction enables testing without cameras
namespace golf_sim::camera::testing {
    class MockCameraHardware : public ICameraHardware {
        // Simulate camera behavior with deterministic responses
    };
    
    class ImageSimulator {
        // Generate synthetic golf ball images for testing
    };
}
```

#### Implementation Priorities

1. **Start with Hardware Abstraction**
   - Create `ICameraHardware` interface first
   - Implement mock camera for testing
   - Generate synthetic test images
   - **Validate**: All tests pass without real hardware

2. **Minimal Bounded Context Extension**
   - Implement ONLY value objects first
   - Test compilation and unit tests
   - Add ONE interface with stub implementation
   - **Validate**: Clean build + passing tests

3. **Incremental Growth with Test Coverage**
   - Add one method at a time
   - Write tests BEFORE implementation
   - Don't move to next layer until current layer has 100% test coverage
   - **Do not change any code not covered by an automated test unless it is changed using a provable refactoring**
   - **Validate**: Each increment maintains test suite health

**Provable Refactoring Resources**:
- **Arlo Belshee's Provable Refactorings**: 
https://github.com/digdeeproots/provable-refactorings

- **Arlo Belshee's Risk Aware Commit Notation**: 
[Arlo's Commit Notation](https://github.com/RefactoringCombos/ArlosCommitNotation) - A notation for small commits messages that show the risk involved in each step

- **Martin Fowler's Refactoring**: [Catalog of Refactorings](https://refactoring.com/catalog/) - Authoritative reference for safe transformations

- **Michael Feathers' "Working Effectively with Legacy Code"** - [Essential techniques for adding tests to untested code](https://archive.org/details/working-effectively-with-legacy-code)

#### Success Criteria for Each Phase
- **Phase 1**: All files compile without errors
- **Phase 2**: All unit tests pass
- **Phase 3**: Integration tests pass  
- **Phase 4**: One legacy usage successfully replaced

### RISK MITIGATION

#### Build Failures
- Make many small and reversible changes
- Test on a clean environment regularly
- Aim to improve the [pipeline](https://github.com/pitraclm/pitrac/blob/a3b3f1baad0ded3066b0c9585ce4879ef6314d18/.github/workflows/camera-tests.yml) test coverage
- Maintain a working baseline with frequent small pull requests and short lived branches

#### Integration Issues  
- Create comprehensive test coverage before migration
- Use feature flags to enable/disable new implementation
- Maintain legacy code until migration complete

#### Performance Issues
- Benchmark existing performance before migration
- Monitor performance after each migration step
- Have a rollback plan for any performance regressions

---


## **Next Steps:**
1. Execute Phase 1: Namespace alignment and business logic extension  
2. Create integration adapters for legacy `GolfSimCamera`
3. Begin incremental migration of specific usage points
4. Leverage existing test framework for validation
