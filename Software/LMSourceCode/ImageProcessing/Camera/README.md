# Camera Bounded Context Tests - Quick Start

## Prerequisites

- CMake (3.6 or later)
- C++ compiler (Visual Studio, MinGW, or Clang)
- Boost libraries with unit_test_framework

## Quick Start

### Option 1: Simple PowerShell Script (Recommended)

```powershell
# From Camera directory
.\build_tests.ps1
```

### Option 2: Manual CMake

```powershell
# Create and enter build directory
mkdir build
cd build

# Configure
cmake ..

# Build
cmake --build .

# Run tests
.\Debug\camera_tests.exe
# or
.\camera_tests.exe
```

### Option 3: Using CTest

```powershell
cd build\tests
ctest -C Debug -V
```

## What Gets Tested

- **Domain Types**: Size, Transform, PixelFormat, ColorSpace
- **Platform Selection**: Automatic platform detection
- **Cross-Platform**: Works on both Windows and Unix
- **Performance**: Basic timing benchmarks
- **Parametric**: Tests with comprehensive data sets

## Test Output Example

```
Running 15 test cases...
Setting up domain type test fixture
*** No errors detected
Tearing down domain type test fixture

===============================================================================
Test suite "CameraBoundedContextTests" passed with:
15 test cases out of 15 passed
```

## Troubleshooting

### Boost Not Found

If CMake can't find Boost:

```powershell
# Option 1: Set environment variable
$env:BOOST_ROOT = "C:\path\to\boost"
.\build_tests.ps1

# Option 2: Specify during configure
mkdir build
cd build
cmake .. -DBOOST_DIR=C:\path\to\boost
cmake --build .
```

### Different Compiler

```powershell
# For Visual Studio
cmake .. -G "Visual Studio 16 2019"

# For MinGW
cmake .. -G "MinGW Makefiles"
```

The test framework is completely self-contained and doesn't require the full PiTrac build system!

## Continuous Integration

The Camera tests run automatically on GitHub Actions:

- **Triggers**: Push to any branch, Pull Requests
- **Platform**: Windows (Visual Studio 2019)
- **Path Filter**: Only runs when Camera code changes
- **Test Suites**: All 25 test cases across 4 test suites

View test results in the [Actions tab](../../actions) of the repository.
