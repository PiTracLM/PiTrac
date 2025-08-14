---
title: Testing Without Hardware
layout: default
nav_order: 3
parent: Troubleshooting
description: Test PiTrac image processing without cameras using test images - perfect for development and debugging
keywords: pitrac testing, no camera, test images, image processing, debugging
toc: true
---

# Testing Without Hardware

Don't have cameras connected? Building PiTrac on a desktop? Want to verify the image processing works? The test processor lets you run PiTrac's ball detection and tracking using test images instead of live camera feeds.

## Why Test Without Hardware?

Testing with images is useful for:
- **Development** - Write and test code without Pi hardware
- **Debugging** - Isolate image processing from hardware issues
- **Learning** - Understand how PiTrac detects and tracks balls
- **CI/CD** - Automated testing in pipelines
- **Verification** - Ensure algorithms work before hardware setup

## Quick Start

The easiest way is through the menu:

```bash
cd ~/Dev
./run.sh
# Choose option 7: Test Image Processing (No Camera)
```

You'll see options:
```
1) Quick Test with Default Images
2) Test with Custom Images
3) List Available Test Images
4) View Latest Results
5) Start Results Web Server
```

Choose option 1 for a quick test. The system will:
1. Load default test images
2. Run ball detection
3. Calculate trajectory
4. Compute spin
5. Generate results with visualizations

## Test Images

PiTrac needs two types of images to calculate ball data:

### Teed Ball Image
- Shows the ball at address (on the tee)
- Used for initial position reference
- Helps detect ball departure

### Strobed Image
- Shows multiple ball positions in flight
- Created by strobe light flashing during exposure
- Used to calculate speed, angles, and spin

### Default Test Images

The system includes test images in `$PITRAC_ROOT/TestImages/`:
```
TestImages/
├── default/
│   ├── teed.png      # Ball on tee
│   └── strobed.png   # Ball in flight with strobe
├── indoor/
│   ├── teed.png      # Indoor lighting example
│   └── strobed.png
└── outdoor/
    ├── teed.png      # Outdoor lighting example
    └── strobed.png
```

## Running Tests

### Quick Test
Uses default images for a fast check:
```bash
~/Dev/scripts/test_image_processor.sh quick
```

Output:
```
[INFO] Running quick test with default images
[INFO] Loading teed ball image: TestImages/default/teed.png
[INFO] Loading strobed image: TestImages/default/strobed.png
[INFO] Processing images...
[SUCCESS] Ball detected at position (512, 384)
[SUCCESS] Trajectory calculated: 12.5° launch, -2.3° azimuth
[SUCCESS] Ball speed: 165.3 mph
[SUCCESS] Spin: 2850 rpm, -5.2° axis
[INFO] Results saved to: test_results/2024-01-15_14-32-05/
```

### Custom Images
Test with your own images:
```bash
~/Dev/scripts/test_image_processor.sh custom /path/to/teed.png /path/to/strobed.png
```

Or use the menu for guided input.

### List Available Images
See what test images are available:
```bash
~/Dev/scripts/test_image_processor.sh list
```

Shows all image sets with descriptions:
```
Available test images:
  default/  - Standard test images
  indoor/   - Indoor lighting conditions
  outdoor/  - Bright outdoor conditions
  custom/   - Your custom test images
```

## Viewing Results

### Console Output
After running a test, you'll see:
```
Ball Detection Results:
  Position: (512, 384)
  Confidence: 0.95

Trajectory Data:
  Launch Angle: 12.5°
  Azimuth: -2.3°
  
Speed Metrics:
  Ball Speed: 165.3 mph
  Club Speed: 112.0 mph (estimated)
  
Spin Data:
  Total Spin: 2850 rpm
  Backspin: 2750 rpm
  Sidespin: -320 rpm
  Axis Tilt: -5.2°
```

### Results Files
Results are saved in timestamped directories:
```
test_results/
└── 2024-01-15_14-32-05/
    ├── summary.json        # All calculated data
    ├── teed_detected.png   # Teed ball with detection overlay
    ├── strobed_path.png    # Flight path visualization
    ├── trajectory.png      # 3D trajectory plot
    └── report.html         # Full HTML report
```

### Web Server
View results in your browser:

```bash
cd ~/Dev
python3 scripts/test_results_server.py
```

Then browse to:
- Local: `http://localhost:8080`
- Network: `http://your-pi-ip:8080`

The web interface shows:
- Image overlays with detection boxes
- Trajectory visualizations
- Shot data tables
- Comparison with previous tests

## Understanding the Output

### Detection Overlays
The processed images show:
- **Green boxes** - Detected balls
- **Red line** - Flight path
- **Blue dots** - Strobe positions
- **Yellow arrow** - Spin axis

### Trajectory Plot
A 3D visualization showing:
- Ball flight path
- Launch angle
- Side angle
- Apex height
- Carry distance

### Data Accuracy
Test results should be close to:
- **Ball speed**: ±2 mph of actual
- **Launch angle**: ±0.5 degrees
- **Spin**: ±100 rpm
- **Azimuth**: ±1 degree

## Creating Test Images

Want to create your own test images for specific scenarios?

### From Real Captures
If you have PiTrac hardware:
1. Capture actual shots
2. Save the images
3. Copy to `TestImages/custom/`
4. Use for testing

### Synthetic Images
Create test images programmatically:
```python
# Example: Create a test teed ball image
import cv2
import numpy as np

# Create blank image
img = np.zeros((1080, 1920, 3), dtype=np.uint8)

# Add ball (white circle)
ball_pos = (960, 800)
ball_radius = 12
cv2.circle(img, ball_pos, ball_radius, (255, 255, 255), -1)

# Add tee (brown rectangle)
tee_top = 812
tee_bottom = 850
tee_left = 955
tee_right = 965
cv2.rectangle(img, (tee_left, tee_top), (tee_right, tee_bottom), (139, 69, 19), -1)

# Save
cv2.imwrite('TestImages/custom/teed.png', img)
```

### Image Requirements
For best results:
- **Resolution**: 1920x1080 or higher
- **Format**: PNG or JPG
- **Ball visibility**: Clear white ball
- **Background**: Dark preferred
- **Strobe**: 3-5 ball positions visible

## Advanced Testing

### Batch Testing
Test multiple image sets:
```bash
for dir in TestImages/*/; do
    echo "Testing $dir"
    ~/Dev/scripts/test_image_processor.sh custom "$dir/teed.png" "$dir/strobed.png"
done
```

### Performance Testing
Measure processing time:
```bash
time ~/Dev/scripts/test_image_processor.sh quick
```

Typical times:
- Ball detection: 50-100ms
- Trajectory calc: 20-40ms
- Spin detection: 100-200ms
- Total: Under 500ms

### Regression Testing
Compare against known good results:
```bash
~/Dev/scripts/test_image_processor.sh compare test_results/baseline/ test_results/current/
```

Shows differences in:
- Detection accuracy
- Calculated values
- Processing time

## Troubleshooting Test Issues

### No Ball Detected
- Check image has clear white ball
- Verify image isn't too dark/bright
- Try adjusting detection threshold in config

### Wrong Trajectory
- Ensure teed ball is stationary
- Check strobed image has multiple positions
- Verify images are from same shot

### Processing Errors
```
[ERROR] Failed to load image
```
- Check file paths are correct
- Verify image format (PNG/JPG)
- Ensure files aren't corrupted

### Python Dependencies
If the test processor fails:
```bash
pip3 install pyyaml opencv-python numpy
```

## Configuration

Test processor settings in `~/Dev/scripts/defaults/test-processor.yaml`:

```yaml
# Detection thresholds
ball_threshold: 200          # Brightness threshold for ball
min_ball_area: 50            # Minimum pixels for ball
max_ball_area: 500           # Maximum pixels for ball

# Processing options
save_visualizations: 1       # Save overlay images
generate_report: 1           # Create HTML report
verbose_output: 0            # Detailed console output

# Web server
server_port: 8080
auto_open_browser: 0
```

## Integration with Development

### Using in VS Code
Add to `.vscode/tasks.json`:
```json
{
    "label": "Test PiTrac Processing",
    "type": "shell",
    "command": "~/Dev/scripts/test_image_processor.sh quick",
    "problemMatcher": []
}
```

### Git Hooks
Test before commits:
```bash
#!/bin/bash
# .git/hooks/pre-commit
~/Dev/scripts/test_image_processor.sh quick || exit 1
```

### CI/CD Pipeline
```yaml
# .github/workflows/test.yml
- name: Test Image Processing
  run: |
    cd Dev
    ./scripts/test_image_processor.sh quick
    # Check results
    test -f test_results/*/summary.json
```

## Tips and Tricks

### Quick Iteration
When developing algorithms:
1. Use same test images repeatedly
2. Save baseline results
3. Compare after changes
4. Use verbose mode for debugging

### Performance Optimization
- Start with low-res images (faster)
- Test algorithm changes
- Scale up to full resolution
- Profile with `time` command

### Creating Test Suites
Organize test images by scenario:
```
TestImages/
├── driver/          # Driver shots
├── iron/            # Iron shots
├── wedge/           # Wedge shots
├── putter/          # Putting (if applicable)
└── edge_cases/      # Difficult scenarios
```

## Next Steps

Testing works? Great! Now:
1. Try different test images to verify robustness
2. Create custom images for your use cases
3. Move to [hardware testing]({% link software/running-pitrac.md %})
4. Start [camera calibration]({% link camera/camera-calibration.md %})

For more debugging tools, see the [Debugging Guide]({% link troubleshooting/debugging-guide.md %}).