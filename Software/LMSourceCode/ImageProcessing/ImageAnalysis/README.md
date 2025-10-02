# Image Analysis Bounded Context

This bounded context provides clean abstractions for golf ball image analysis capabilities, enabling the system to detect teed balls, movement, ball flight analysis, and ball reset detection. It follows Domain-Driven Design principles with a roadmap for porting over the physics-based speed detection.

### Requires Camera Calibration Integration

Once merged - the Camera bounded context can be used to create the calibration parameters. This will enable the 3D coordinate transformations found in gs_camera.cpp
  
**Files to modify**: `domain/value_objects.hpp`, `domain/analysis_results.hpp`  

**Next Steps**: See [Implementation Roadmap](#implementation-roadmap) below for the complete TODO action plan.

## Architecture

---

## Architecture Detail

### Clean Architecture Implementation

The implementation follows Clean Architecture principles with layers for abstracting implementation details:

```
ImageAnalysis/
├── domain/              # Business rules and entities (technology-agnostic)
│   ├── value_objects.hpp    # BallPosition, ImageBuffer, BallState, CameraCalibrationRef
│   ├── analysis_results.hpp # TeedBallResult, MovementResult, FlightAnalysisResult  
│   └── interfaces.hpp       # IImageAnalyzer interface
├── application/         # Use cases and application services
│   └── image_analysis_service.hpp # High-level service orchestration
├── infrastructure/      # Implementation details (frameworks, external libraries)
│   ├── opencv_image_analyzer.hpp/cpp  # OpenCV/Hough circles implementation
│   └── ml_image_analyzer.hpp          # ML/AI framework (YOLO, TensorFlow, etc.)
├── image_analysis.hpp   # Public API (main entry point)
└── image_analysis.cpp   # API implementation

External Dependencies:
├── Camera/              # Camera bounded context (separate PR)
│   └── calibration_service.hpp # Provides CameraCalibration objects
```

### Current Speed Detection Architecture

**Current Implementation** (DDD Phase - Placeholder Algorithms):
> **Note**: These are simplified placeholder algorithms designed to validate the approval test framework and clean DDD architecture. They provide example functionality until the existing 3D physics implementation is ported over.

- **2D pixel-space calculations**: Simple x/y displacement over time (placeholder for 3D world coordinates)
- **Fixed timing**: Hardcoded 1ms intervals (`TEMPORAL_SPACING_US = 1000`) (placeholder for dynamic hardware timing)
- **No camera correction**: Direct pixel-to-meter conversion without perspective adjustment (placeholder for full 3D calibration)
- **Basic physics**: Linear velocity calculation without spin (placeholder)

**Target Implementation** (After Physics Integration):
- **3D world coordinates**: Uses `gs_camera.cpp` coordinate transformation algorithms
- **Dynamic timing**: Hardware strobe interval synchronization from `GetStrobeInterval()`
- **Camera calibration**: 3D perspective correction using focal length and sensor dimensions
- **Physics**: 3D Euclidean distance with air resistance compensation from gs_camera.cpp
`GolfSimCamera::CalculateBallVelocity()`

## Usage

### Basic Usage

```cpp
#include "ImageAnalysis/image_analysis.hpp"

using namespace golf_sim::image_analysis;

// Create a service with OpenCV implementation
auto service = CreateAnalysisService("opencv");

// Analyze an image for teed ball
domain::ImageBuffer image = ...; // Your image data
auto result = service->AnalyzeTeedBall(image);

if (result.state == domain::BallState::TEED) {
    std::cout << "Ball detected at (" 
              << result.position->x_pixels << ", " 
              << result.position->y_pixels << ")" << std::endl;
}
```

### Advanced Configuration

```cpp
// Create factory and configure manually
auto factory = CreateAnalyzerFactory();
auto service = std::make_unique<application::ImageAnalysisService>(std::move(factory));

application::AnalyzerConfig config;
config.type = "opencv";
config.parameters["min_radius"] = "10";
config.parameters["max_radius"] = "100";
config.enable_debug_output = true;

service->Configure(config);
```

### Movement Detection

```cpp
// Detect ball movement (shot in progress)
std::vector<domain::ImageBuffer> image_sequence = ...;
domain::BallPosition reference_position = ...;

auto movement_result = service->DetectMovement(image_sequence, reference_position);
if (movement_result.movement_detected) {
    std::cout << "Shot detected! Movement confidence: " 
              << movement_result.movement_confidence << std::endl;
}
```

### Current Ball Flight Analysis (2D Approach)

**Current State**: Basic 2D pixel-space velocity calculation with fixed timing
- Returns velocity vector in m/s with ±10-20% accuracy
- Uses hardcoded temporal spacing assumptions
- No camera perspective correction applied
- Suitable for development and testing phases

## Available Implementations

### OpenCV Implementation (`opencv`)
- **Technology**: OpenCV Hough Circle Detection
- **Dependencies**: Existing `BallImageProc` and `GolfSimCamera` classes

### Example of a theoretical alternative Implementation (`machine learning` )
- **Technology**: YOLO v5/v8, TensorFlow Lite, PyTorch Mobile
- **Use Cases**: Experimentation, challenging lighting/backgrounds
- **Dependencies**: Model files, ML framework libraries

## Adding or Testing New Implementations

If you wanted to add a new analyzer implementation, but still keep existing logic working:
For example using a trained model built from Machine Learning frameworks like TensorFlow:

1. Create a class implementing `domain::IImageAnalyzer`
2. Place it in `infrastructure/` folder
3. Update the factory in `image_analysis.cpp`
4. Add configuration support in application layer

Example:

```cpp
namespace golf_sim::image_analysis::infrastructure {
    
    class CustomImageAnalyzer : public domain::IImageAnalyzer {
    public:
        domain::TeedBallResult AnalyzeTeedBall(
            const domain::ImageBuffer& image,
            const std::optional<domain::BallPosition>& expected_position
        ) override {
            // Your implementation here
        }
        
        // ... implement other methods
    };
}
```

## Integration with Existing Code

This bounded context is designed to integrate with the existing PiTrac system:

- **OpenCV Implementation**: Wraps existing `BallImageProc` logic
- **Camera Integration**: Compatible with `GolfSimCamera` strobed analysis
- **Result Compatibility**: Can be converted to existing `GolfBall` objects
- **Configuration**: Uses existing JSON configuration patterns

## Testing

```cpp
// Unit tests should focus on domain logic
TEST(ImageAnalysisTest, BallPositionCalculation) {
    domain::BallPosition pos1(100, 200, 15);
    domain::BallPosition pos2(105, 205, 15);
    
    EXPECT_NEAR(pos1.DistanceFrom(pos2), 7.07, 0.1);
}

// Integration tests should verify adapter behavior  
TEST(OpenCVAnalyzerTest, DetectsTeedBall) {
    infrastructure::OpenCVImageAnalyzer analyzer;
    domain::ImageBuffer test_image = LoadTestImage("teed_ball.jpg");
    
    auto result = analyzer.AnalyzeTeedBall(test_image);
    EXPECT_EQ(result.state, domain::BallState::TEED);
}
```

## Performance Considerations

- **Memory Usage**: Minimal overhead beyond underlying implementations
- **Threading**: All implementations are thread-safe for read operations

## Migration Guide

### Migration Steps

**Phase 1: Current Migration (Basic DDD)**
- Replace `BallImageProc` direct usage with DDD service interface
- Maintain existing algorithms during architectural transition
- Clean separation of concerns with immutable value objects

**Phase 2: Enhanced Migration (3D Physics Integration)**
- Integrate Camera bounded context for calibration parameters
- Port 3D coordinate transformation logic from `gs_camera.cpp`
- Implement physics-based velocity calculations
- Target: Preserve legacy system behavior

### Migration Timeline

1. **Immediate** (Current): Create DDD interface with enough logic to get automated tests running in pipelines
2. **Phase 2**: Camera calibration integration after Camera bounded context PR
3. **Phase 3**: Port existing behavior to Bounded Context
4. **Phase 3a** Add Feature switching to divert code to new BC logic in NonProd or for UAT Testing
5. **Phase 3b** Complete migration deprecating `gs_camera.cpp`

---

## Additional Resources

### Related Documentation
- **Camera Bounded Context**: 
[Pull Request](https://github.com/pitraclm/pitrac/pull/87) Provides camera abstractions and calibration data.

## Camera Bounded Context Integration

### Architecture Decision: Separation of Concerns

The Camera bounded context (separate PR) will handle:
- **Camera hardware management** (initialization, configuration)
- **Calibration parameters** (focal length, sensor dimensions, distortion coefficients)
- **Camera-specific operations** (frame capture, timing synchronization)

ImageAnalysis bounded context will:
- **Consume calibration data** from Camera context via dependency injection
- **Focus on ball detection and tracking** algorithms
- **Perform coordinate transformations** using Camera-provided parameters
- **Calculate velocities and trajectories** in world coordinates

### Integration Pattern

**Clean separation between bounded contexts:**
- ImageAnalysis focuses on ball detection and velocity calculation algorithms
- Camera context provides calibration data via dependency injection
- Hardware timing integration through service interfaces
- No duplication of camera management logic across contexts
- No requirement for physical cameras or strobe capture of real ball flight when working on the code

**Key Benefits:**
1. **Single Responsibility**: Each context handles its domain expertise
2. **Reduced Coupling**: Clean dependency direction (ImageAnalysis → Camera)
3. **Reusability**: Camera calibration shared across multiple contexts
4. **Testability**: Mock camera services for isolated testing

### Success Criteria

#### ✅ Phase 1 Complete When:
- [ ] **Behavior maintained** algorithms in the legacy implementation can be duplicated
- [ ] **Automated tests passing** Approval tests and unit tests give developer feedback to protect against regression
- [ ] **Camera BC integrated** into analysis pipeline - no physical camera required in the build agents

---
