---
title: Auto-Calibration
layout: default
nav_order: 2
parent: Cameras
---

# Auto-Calibration

Auto-calibration is the recommended way to calibrate PiTrac cameras. It automatically determines focal lengths and camera angles through a 4-step web wizard - no tape measures, no manual JSON editing, no shell scripts.

## What Auto-Calibration Does

**Calculates automatically**:
- **Focal length** for each camera (in pixels, typically 800-1200 for 6mm lens)
- **Camera angles** (X and Y tilt in degrees)

**Writes to configuration**:
- Updates `~/.pitrac/config/calibration_data.json` with results
- Preserves these values across config resets
- No manual file editing required

**Previous method** (deprecated): Manual measurements, Excel spreadsheets, tape measures, angle calculations by hand. If you're still doing that, stop. Use this instead.

## Physical Setup

Before you can run auto-calibration in the web UI, you need a calibration rig with golf balls at known positions.

### 1. Print the Calibration Rig

**Files**: [GitHub - Calibration Rig STL files](https://github.com/PiTracLM/PiTrac/tree/main/3D%20Printed%20Parts/Enclosure%20Version%202/Calibration%20Rig)

The rig works with both V1.0 and V2.0 enclosures. Only difference is the measurement values you'll configure.

**Print Settings**:
- Material: PETG preferred (less warping), PLA works fine
- Infill: 15% standard
- Supports: Only on the Ball 1 floor mount (green area in STL)
- Filament: < 100 grams

**Assembly**:
- Push pieces together snugly (can be tight - that's normal)
- Make sure connections are fully seated for accurate dimensions
- Ball 1 (right-most) sits on floor, Ball 2 (mid-air)

### 2. Position the Rig

**Version 2.0 Enclosure**:
- Insert tab into square hole in bottom front of enclosure
- Check that rig is square to the enclosure

**Version 1.0 Enclosure**:
- Center tab underneath lowest camera
- Place tab on diagonal part of enclosure
- Align end edge of rear part with outside of lower enclosure section

**Use a square** (carpenter's square or similar) to ensure rig is perpendicular to the enclosure. Precision here improves calibration accuracy.

### 3. Aim the Cameras

**Camera 1** (Tee Camera):
- Point directly at Ball 1 (on floor)
- Doesn't need to be perfect - calibration figures out exact angle
- Just get it close

**Camera 2** (Flight Camera):
- Point straight out from monitor
- Aim slightly below Ball 2 (mid-air ball)
- Again, close is fine

**Tighten camera mount screws** once aimed. Cameras moving after calibration = need to recalibrate.

### 4. Configure Ball Positions

The calibration wizard needs to know where the balls are relative to the cameras. These measurements are in the Configuration page under "Calibration" settings.

**For Version 1.0 Enclosure** (with standard rig):

```
kAutoCalibrationBallPositionFromCamera1Meters: [-0.525, -0.275, 0.45]
kAutoCalibrationBallPositionFromCamera2Meters: [0.0, 0.095, 0.435]
```

**For Version 2.0 Enclosure** (with standard rig):

```
kAutoCalibrationBallPositionFromCamera1Meters: [-0.504, -0.234, 0.491]
kAutoCalibrationBallPositionFromCamera2Meters: [0.0, 0.088, 0.466]
```

**Coordinate system**:
- X: Positive = right (from camera view)
- Y: Positive = up
- Z: Positive = outward (away from camera)
- Units: Meters

If you built your own rig, measure from camera lens center to ball center and enter those values.

**These settings are in the web Configuration page** - you don't need to edit JSON files manually.

---

## Running Auto-Calibration (Web UI)

### Step 1: Open Calibration Wizard

1. Navigate to web dashboard (usually `http://raspberrypi.local:8080`)
2. Click menu (3 dots) → **Calibration**
3. Select which camera(s) to calibrate:
   - **Camera 1** - Recommended starting point (~30 seconds)
   - **Camera 2** - Do this second (~2 minutes, yes really)
   - **Both** - Only after you've done each individually

**Always calibrate Camera 1 first.** It's faster and gives you immediate feedback if something's wrong.

Click **Next**.

### Step 2: Verify Ball Placement

This step confirms PiTrac can actually see the ball before trying to calibrate.

**For each camera**:

1. Click **Check Ball Location**
   - Green checkmark = Ball found at (X, Y) pixel coordinates
   - Red X = Ball not detected

2. Click **Capture Still Image**
   - Shows what camera sees
   - Ball should be visible and roughly centered
   - Focus should be good (twist M12 lens if not)

**If ball detection fails**:
- Check lighting (turn on strobes, adjust camera gain in Configuration)
- Verify ball is actually on the rig
- Make sure camera is aimed at ball
- Check focus

Once both buttons show success, click **Next**.

### Step 3: Run Calibration

Pick calibration method:

**Auto Calibration** (Recommended)
- Fully automatic
- Click "Start Auto Calibration"
- Watch progress bar
- Wait for completion

**Manual Calibration** (Advanced)
- For troubleshooting when auto fails
- Same process but longer timeout (3 minutes vs 30-120 seconds)
- Rarely needed

**What happens during calibration**:

**Camera 1** (~30 seconds):
1. Captures 10 images of the ball (configurable)
2. Detects ball center in each image using Hough Circle Transform
3. Calculates focal length from: `f = (distance × sensor_width × (2 × radius_px / resolution_x)) / (2 × ball_radius_m)`
4. Averages focal lengths across all successful samples
5. Validates result (focal length must be 2.0-50.0mm, radius 0-10k pixels)
6. Computes camera X and Y angles using final focal length
7. Sends results to web server via HTTP API
8. Updates configuration files

**Camera 2** (~90-120 seconds):
- Same algorithm as Camera 1
- Takes longer due to two-process workflow in single-Pi mode:
  - Background process captures images from Camera 2
  - Foreground process performs calibration
  - Background must initialize before foreground starts (adds ~30 seconds)

**Completion Detection**:
The system uses a hybrid approach to detect when calibration finishes:

1. **API Callbacks** (Primary) - C++ binary sends HTTP PUT requests when successful:
   - `PUT /api/config/gs_config.cameras.kCamera1FocalLength` (focal length value)
   - `PUT /api/config/gs_config.cameras.kCamera1Angles` (X, Y angles array)
   - Both must be received for success

2. **Process Exit** (Secondary) - Monitors C++ process exit code
   - Code 0 = potential success (but may have failed internally)
   - Non-zero = definite failure

3. **Output Parsing** (Validation) - Checks for failure strings in output:
   - "Failed to AutoCalibrateCamera"
   - "ONNX detection failed - no balls found"
   - "GetBall() failed to get a ball"
   - "Could not DetermineFocalLengthForAutoCalibration"

4. **Timeout** (Safety Net)
   - Camera 1: 30 seconds
   - Camera 2: 120 seconds
   - Prevents hanging forever if process stalls

**Progress Indicators**:
- **API Callbacks Received** - Best outcome, all data received
- **Process Exit** - Process finished but may not have sent API callbacks
- **Timeout** - Process didn't complete in time, check logs

**Don't**:
- Move the ball
- Move the cameras
- Close the browser
- Stop PiTrac while calibrating

### Step 4: Review Results

You'll see results for each camera:

**Status**: Success or Failed

**Completion Method**: How calibration finished
- "api" - API callbacks received (best)
- "process" - Process exited cleanly
- "timeout" - Took too long (check logs)

**Focal Length**: Number in pixels (e.g., "1025.347")
- Typical range for 6mm lens: 800-1200
- If way outside this range, something's wrong

**Camera Angles**: `[X_angle, Y_angle]` in degrees (e.g., "[12.45, -6.78]")
- X angle: Horizontal tilt
- Y angle: Vertical tilt
- Typical range: -20° to +20°

**What to do**:
- Success? Click "Return to Dashboard" and test it out
- Failed? Check troubleshooting section below

---

## Verification

After calibration, verify it worked:

### Ball Location Check

**From web UI** (Testing Tools):
1. Leave ball on rig
2. Navigate to Testing Tools
3. Click "Check Ball Location" for Camera 1

**Expected**: Ball position in 3D should match your configured calibration position (within ~10mm)

**Example**:
- Configured: [-0.525, -0.275, 0.45] meters
- Detected: [-0.523, -0.271, 0.448] meters
- Difference: ~7mm - Good!

If detected position is way off (>50mm difference), calibration likely failed even if it said success.

### Test Shots

Hit some balls and check if speeds and angles look reasonable:
- Driver swing: 80-120 mph ball speed
- 7-iron swing: 60-90 mph ball speed
- Launch angles: 5-20° depending on club

If you're getting 300 mph or 5 mph, calibration is wrong. Recalibrate.

---

## Troubleshooting

### Ball Not Detected in Step 2

**Lighting Issues**:
- Turn on strobes (you should hear them click)
- Increase camera gain in Configuration → Cameras → kCamera1Gain (try 8-12)
- Make sure room isn't too bright (IR strobes work better in darker rooms)

**Focus Issues**:
- Twist M12 lens focus ring
- Capture still images to check focus
- Ball should have sharp edges, not blurry

**Positioning Issues**:
- Verify ball is actually on rig
- Check camera is aimed at ball
- Make sure nothing is blocking camera view

### Calibration Times Out

**Camera 1** (should take ~30 seconds):
- Ball moved during calibration? Try again, keep it still
- Camera type set wrong? Check Configuration → Cameras → Auto Detect
- Check logs (Logging page) for errors

**Camera 2** (should take 90-120 seconds):
- **This is normal.** Camera 2 legitimately takes 2 minutes. Be patient.
- Background process needs time to initialize and capture images
- If it goes past 2 minutes, check logs for errors

**Both Cameras**:
- System overloaded? Check `top` on Pi - CPU should not be at 100%
- Try manual calibration mode (longer timeout, may succeed)

### Results Look Wrong

**Focal length way off** (< 800 or > 1200 for 6mm lens):
- Wrong lens type configured? Check Configuration → Cameras → Lens Choice
- Measured ball positions wrong? Verify rig measurements
- Ball detection failing? Check captured images in Testing Tools

**Angles look weird** (> 30° off from expected):
- Cameras moved during calibration
- Rig not square to enclosure
- Wrong enclosure version measurements (V1.0 vs V2.0)

**Speed readings wrong after calibration**:
- Ball position measurements incorrect
- Try recalibrating with verified measurements
- Check if ball moved between calibration attempts

### Calibration Succeeds But Doesn't Apply

**Check**:
- Calibration data should be in `~/.pitrac/config/calibration_data.json`
- Restart PiTrac LM (Stop/Start in web UI)
- Configuration → show focal length/angles - should match calibration results

**If still using old values**:
- User settings may be overriding calibration data
- Check Configuration → Show Diff to see what's overridden
- Remove camera angle/focal length entries from user settings if present

---

## Configuration Files

Auto-calibration writes to these locations:

**Calibration Results**: `~/.pitrac/config/calibration_data.json`
```json
{
  "gs_config": {
    "cameras": {
      "kCamera1FocalLength": 1025.347,
      "kCamera1Angles": [12.45, -6.78],
      "kCamera2FocalLength": 1050.123,
      "kCamera2Angles": [8.7, -3.1]
    }
  }
}
```

**Backups**: Old config backed up to:
- `~/.pitrac/config/calibration_data.json.backup.<timestamp>`

**Runtime Config**: Results merged into:
- `~/.pitrac/config/generated_golf_sim_config.json` (what C++ binary reads)

**You never need to edit these manually.** The web UI handles everything.

---

## Advanced Topics

### Number of Samples

**Default**: 10 images per camera

**Adjust**: Configuration → Calibration → kNumberPicturesForFocalLengthAverage

**Trade-off**:
- More samples = more accurate but slower
- Fewer samples = faster but may be less accurate
- 10 is a good balance

### Custom Calibration Rigs

If you built your own rig:

1. Measure from camera lens center to ball center (X, Y, Z in meters)
2. Enter in Configuration → Calibration:
   - `kAutoCalibrationBallPositionFromCamera1Meters`
   - `kAutoCalibrationBallPositionFromCamera2Meters`
3. Run calibration wizard

**Measurement tips**:
- Use CAD software (FreeCAD, Fusion 360) for precision
- Measure to center of ball, not edge
- Measure from lens center, not camera front
- Use meters, not inches or millimeters

### Lens Distortion

Auto-calibration handles minor lens distortion automatically. For severe distortion (fisheye lenses), you may need additional undistortion steps.

Current calibration assumes reasonable lens quality. The 6mm M12 lenses that come with cameras work fine.

---

## When to Recalibrate

**Required**:
- First time setup
- After moving cameras
- After changing lenses
- After dropping/bumping enclosure

**Not required**:
- Every time you use PiTrac
- After adjusting camera gain
- After tweaking ball detection settings
- After software updates

Calibration data persists across reboots and config changes. Don't recalibrate unless you have a reason.

---

## Next Steps

1. **Calibrate Camera 1** - Takes 30 seconds
2. **Calibrate Camera 2** - Takes 2 minutes
3. **Verify results** - Check ball location matches expected position
4. **Test shots** - Hit some balls, verify speeds look reasonable
5. **Fine-tune** - Adjust gain, search center, etc. as needed

Good calibration is the foundation of accurate ball tracking. Take your time with the physical setup - it's worth it.
