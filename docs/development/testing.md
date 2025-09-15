---
layout: default
title: Testing Framework
parent: Development Guide
nav_order: 6
---

# PiTrac Testing Framework

Testing in PiTrac is performed through the **web UI at port 8080**. The web interface provides a comprehensive testing suite with real-time feedback and logging.

## Web UI Testing Interface

### Access Testing Tools

1. Start the web server: `pitrac web start`
2. Navigate to `http://your-pi-ip:8080`
3. Click on the **Testing** section in the navigation menu

### Available Testing Categories

The web UI organizes testing tools into categories:

#### Hardware Tests
- **Strobe Pulse Test** - Test IR strobe functionality with configurable duration
- **Camera Still Capture** - Capture test images from Camera 1 or Camera 2
- **Ball Location Tests** - Verify ball detection for each camera

#### Calibration Tests
- **Ball Position Verification** - Check ball placement accuracy
- **Camera Alignment Tests** - Verify camera positioning and angles

#### System Tests
- **Test Image Processing** - Run detection on sample images
- **Automated Test Suite** - Comprehensive system validation
- **Quick Test** - Fast image processing test without cameras

#### Connectivity Tests
- **GSPro Connection Test** - Verify simulator connectivity
- **ActiveMQ Status Check** - Test message broker connection

### Test Execution Features

- **Real-time Output** - View test results as they execute
- **Background Execution** - Tests run without blocking the UI
- **Timeout Management** - Configurable test timeouts
- **Result Storage** - Test results saved with timestamps
- **Log Integration** - View detailed logs for each test

## C++ Unit Tests (Development Only)

For developers working on the core C++ codebase:

### Framework
PiTrac uses the **Boost Test Framework** for C++ unit testing.

### Test Locations
- `Software/LMSourceCode/ImageProcessing/Camera/tests/`
- `Software/LMSourceCode/ImageProcessing/ImageAnalysis/tests/`

### Building Tests
```bash
# Using CMake (for test modules)
cd Software/LMSourceCode/ImageProcessing/Camera
mkdir build && cd build
cmake ..
make
ctest
```

### Approval Testing
The ImageAnalysis module uses approval testing for validating image processing results against known good outputs.

## Python Web Server Tests

The web server includes its own test suite:

```bash
cd Software/web-server
# Install dev dependencies
pip install -r requirements-dev.txt
# Run tests
python -m pytest tests/
```

Test coverage includes:
- API endpoint testing
- WebSocket functionality
- Configuration management
- Message parsing
- Process management

## Test Image Library

PiTrac includes sample test images at `/usr/share/pitrac/test-images/` for testing without hardware:
- Ball detection samples
- Spin calculation samples
- Various lighting conditions

These images are automatically used by the web UI's testing tools.

## Development Testing Workflow

1. **Make code changes**
2. **Rebuild**: `sudo ./build.sh dev` (in packaging/)
3. **Access web UI**: `http://your-pi-ip:8080`
4. **Navigate to Testing section**
5. **Run relevant tests**
6. **View results and logs in real-time**

## Best Practices

1. **Use the web UI for all testing** - Better user experience and feedback
2. **Run tests after installation** - Verify system setup
3. **Test before calibration** - Ensure hardware is working
4. **Check logs for details** - Web UI Logs section shows test output
5. **Use test images for development** - Test without hardware setup

## Troubleshooting Tests

### Test Failures in Web UI

1. Check the **Logs** section for detailed error messages
2. Verify hardware connections (cameras, GPIO)
3. Ensure ActiveMQ is running: Check status indicators
4. Confirm PiTrac process is not already running

### Camera Test Issues

- Verify camera is connected properly
- Check `/boot/firmware/config.txt` (Pi 5) for `camera_auto_detect=1`
- Use `rpicam-hello --list-cameras` to verify detection

### Connectivity Test Failures

- Check network configuration
- Verify firewall settings
- Ensure simulator software is running
- Check port availability (GSPro uses specific ports)

## Test Result Interpretation

The web UI provides clear pass/fail indicators:
- **Green checkmark** - Test passed
- **Red X** - Test failed
- **Yellow warning** - Test completed with warnings
- **Spinner** - Test in progress

Detailed results and logs are available by clicking on each test result.