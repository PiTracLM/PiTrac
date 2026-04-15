# Approval Testing with Real PiTrac Images

This directory contains approval tests that use actual PiTrac test images to validate the ImageAnalysis bounded context. The approval testing approach creates baseline artifacts that can be compared against future test runs to detect regressions.

## Overview

Approval testing works by:

1. **First Run**: Creates "approved" baseline files representing expected behavior
2. **Subsequent Runs**: Compares current results against approved baselines
3. **Failure**: When outputs differ, manual review determines if change is intended or a regression

## Test Images Used

The tests use real PiTrac images from `C:\kata\PiTrac\Software\LMSourceCode\Images\`:

- `log_ball_final_found_ball_img.png` - Ball detection scenario
- `gs_log_img__log_ball_final_found_ball_img.png` - Golf simulation log image
- `log_cam2_last_strobed_img.png` - Strobed ball capture
- `log_cam2_last_strobed_img_232_fast.png` - Fast strobed capture
- `spin_ball_1_gray_image1.png` - Spin analysis scenario 1
- `spin_ball_2_gray_image1.png` - Spin analysis scenario 2
- `log_ball_final_found_ball_img_232_fast.png` - Fast ball detection

## Generated Artifacts

For each test, the framework generates:

### Text Artifacts

- `{test_name}.approved.txt` - Baseline expected results
- `{test_name}.received.txt` - Current test run results

### Visual Artifacts

- `{test_name}.approved.png` - Baseline visualization with detected ball highlighted
- `{test_name}.received.png` - Current run visualization

## Running Approval Tests

### Using the Build Script (Recommended)

```powershell
# From ImageAnalysis directory
.\build_tests.ps1 -Verbose

# Run only approval tests
.\build_tests.ps1 -Verbose -ApprovalOnly
```

### Using CMake Directly

```powershell
# Build and run all tests
mkdir build
cd build
cmake ..
cmake --build .
ctest --verbose

# Run only approval tests
./test_approval_with_pitrac_images.exe
```

### Using Custom Targets

```powershell
# Run all tests
cmake --build . --target run_tests

# Run only approval tests
cmake --build . --target run_approval_tests
```

## Approval Workflow

### Initial Setup (First Time)

1. Run the tests - they will create baseline `.approved.txt` and `.approved.png` files
2. Manually review the generated artifacts to ensure they represent correct behavior
3. Commit the approved files to version control

### Regular Development

1. Run tests normally - they compare against approved baselines
2. If tests fail, examine the differences:
    - Review `.received.txt` vs `.approved.txt`
    - Compare `.received.png` vs `.approved.png`
3. If changes are intended (new features, improvements):
    - Copy `.received.*` files over `.approved.*` files to update baselines
    - Commit the updated approved files
4. If changes are unintended:
    - Fix the code to match expected behavior

### Updating Baselines

When intentional changes require new baselines:

```powershell
# Copy received files to approved (Windows)
copy "approval_artifacts\*.received.txt" "approval_artifacts\*.approved.txt"
copy "approval_artifacts\*.received.png" "approval_artifacts\*.approved.png"

# Or use PowerShell
Get-ChildItem "approval_artifacts\*.received.*" | ForEach-Object {
    $approved = $_.FullName -replace '\.received\.', '.approved.'
    Copy-Item $_.FullName $approved -Force
}
```

## Analysis Result Format

Each test generates a detailed analysis summary including:

```
=== Image Analysis Result Summary ===
Timestamp: 1000000 microseconds
Source ID: test_name
Processing Duration: 42.5 ms
Ball Detected: YES
Ball Position:
  X: 320.50 pixels
  Y: 240.75 pixels
  Radius: 15.25 pixels
  Confidence: 0.885
  Detection Method: HoughCircles
  Valid: YES
Quality Metrics:
  Overall Score: 0.823
  Confidence Score: 0.885
  Analysis Successful: YES
==========================================
```

## Integration with CI/CD

The approval tests are designed for local development and validation. For CI/CD:

1. **Commit approved baselines** to version control
2. **CI runs compare mode** - tests fail if received ≠ approved
3. **Manual review** required for baseline updates
4. **No automatic baseline updates** in CI to prevent regressions

## Troubleshooting

### Test Images Not Found

- Verify PiTrac images exist at `C:\kata\PiTrac\Software\LMSourceCode\Images\`
- Check file permissions and accessibility

### Approval Artifacts Directory

- Directory is automatically created at `approval_artifacts\`
- Ensure write permissions in test directory

### Boost Test Framework Issues

- Tests use same Boost configuration as Camera bounded context
- Supports both linked and header-only Boost modes

## Benefits

1. **Real Data Validation**: Uses actual golf ball images from PiTrac system
2. **Regression Detection**: Automatically catches unintended behavior changes
3. **Visual Verification**: Generated images show ball detection results
4. **Baseline Management**: Simple workflow for updating expected results
5. **Integration Ready**: Tests the full image analysis pipeline end-to-end

This approval testing framework provides confidence that the ImageAnalysis component correctly processes real golf ball scenarios while making it easy to validate changes and detect regressions.

# Image Analysis Approval Testing Documentation

## Overview

This document describes the approval testing framework implemented for the PiTrac Image Analysis bounded context. Approval testing validates that our image analysis algorithms produce consistent, expected results when processing real PiTrac golf ball images.

## Test Implementation Status ✅ COMPLETED

### Successfully Implemented

- ✅ **Test Framework**: Complete approval testing framework using real PiTrac images
- ✅ **Image Integration**: All 7 PiTrac test images successfully integrated
- ✅ **Baseline Generation**: All baseline artifacts created and validated
- ✅ **Build Integration**: CMake targets and PowerShell build script support
- ✅ **Documentation**: Comprehensive testing documentation

### Test Results Summary

**Build Status**: ✅ All tests passing (8/8 test cases)
**Execution Time**: ~2 seconds for complete approval test suite
**Coverage**: 7 real PiTrac images + 1 movement analysis integration test

## Architecture

### Key Components

1. **ApprovalTestFixture**: Core test infrastructure
2. **Image Loading**: Automated PiTrac image discovery and loading
3. **Result Analysis**: Detailed result summarization and visualization
4. **Artifact Management**: Received/approved artifact comparison system

## Test Results Analysis

### Ball Detection Performance

Based on our baseline runs, the system demonstrates:

1. **High Confidence Detection**:
    - `log_ball_final_found_ball_img.png`: Ball detected at (998.5, 593.5) with 80% confidence
    - State: TEED, Method: opencv_hough_circles

2. **Low Confidence Cases**:
    - `spin_ball_1_gray_image1.png`: Ball state ABSENT with 10% confidence
    - Detected circle at (56.5, 54.5) but classified as insufficient confidence

3. **Movement Analysis**:
    - Successfully detects movement between strobed images
    - Movement magnitude: 187.359 pixels with 100% confidence
    - Uses opencv_optical_flow method

### Baseline Artifacts Generated

All baseline files have been created in `tests/approval_artifacts/`:

- **Text Summaries**: 8 `.approved.txt` files with detailed analysis results
- **Visual Artifacts**: 7 `.approved.png` files with detected ball highlighting
- **Movement Analysis**: 1 movement analysis result for strobed image sequence

## Build Integration Status

### CMake Integration ✅

- Approval test executable: `test_approval_with_pitrac_images`
- Test labels: `approval`, `images`, `pitrac`
- Custom target: `approval_tests`

### PowerShell Build Script ✅

- Full integration with `build_tests.ps1`
- Automatic execution in test suite
- Clean emoji-free output for cross-platform compatibility

## Usage

### Running Approval Tests

```powershell
# Run all tests including approval tests
.\build_tests.ps1

# Run only approval tests
cmake --build build --target test_approval_with_pitrac_images
.\build\Debug\test_approval_with_pitrac_images.exe
```

## Enhanced Developer Experience

### Approval Helper Script

We've created `approve_changes.ps1` to streamline the approval testing workflow:

```powershell
# Quick overview of pending changes
.\approve_changes.ps1

# List all pending changes
.\approve_changes.ps1 -ListChanges

# Open all received images for visual review
.\approve_changes.ps1 -ViewImages

# Approve changes for a specific test
.\approve_changes.ps1 -TestName 'spin_ball_1'

# Approve all changes (use carefully!)
.\approve_changes.ps1 -ApproveAll
```

### Automated VS Code Integration

When approval tests fail, the framework automatically:

1. **Launches VS Code diffs** for text file comparisons
2. **Opens image files** for visual comparison
3. **Creates empty baselines** for missing files to show differences
4. **Provides copy commands** for easy approval

### Missing Baseline Handling

When a baseline file is missing (e.g., after deletion), the test will:

1. **Detect the missing file** and fail appropriately
2. **Create a temporary empty baseline** for comparison
3. **Launch VS Code** to show the received content vs empty baseline
4. **Provide clear instructions** for approving the new baseline

### Developer Workflow Example

```powershell
# 1. Run tests to see failures
.\build_tests.ps1

# 2. Review pending changes
.\approve_changes.ps1

# 3. Use provided diff commands to review specific changes
code --diff "approved.txt" "received.txt"

# 4. Approve specific test changes
.\approve_changes.ps1 -TestName 'ball_detection'

# 5. Or approve all if you've reviewed them
.\approve_changes.ps1 -ApproveAll

# 6. Re-run tests to confirm
.\build_tests.ps1
```

## Benefits of Enhanced Framework

1. **🚀 Faster Workflow**: Helper script automates common approval tasks
2. **👁️ Visual Review**: Automatic VS Code integration for easy comparison
3. **🛡️ Safety**: Clear warnings and confirmation steps for bulk operations
4. **📊 Better Visibility**: Clear overview of what needs approval
5. **🔧 Missing File Handling**: Proper detection and workflow for deleted baselines
6. **🧹 Automatic Cleanup**: Received files are cleaned up after approval

This enhanced approval testing framework provides confidence that the ImageAnalysis component correctly processes real golf ball scenarios while making it easy to validate changes and detect regressions.
