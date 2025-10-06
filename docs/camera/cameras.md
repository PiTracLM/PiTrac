---
title: Cameras
layout: default
nav_order: 5
has_children: true
---

# Camera System

PiTrac uses two Raspberry Pi cameras to track golf balls in 3D. One camera watches the teed ball, the other tracks the ball in flight. Both cameras capture high-speed images synchronized with IR strobes to freeze the ball mid-motion.

## Supported Cameras

### Recommended (Current Generation)

**InnoMaker CAM-MIPI327RAW (IMX296 Mono)**
- Same IMX296 sensor, monochrome version
- Slightly better light sensitivity
- Same resolution and frame rate
- Requires special trigger tool for synchronization
- Good for low-light setups

**Pi Global Shutter Camera (IMX296 Color)** - Alternative
- Sony IMX296 sensor with global shutter (no rolling shutter artifacts)
- 1456 x 1088 resolution
- Native 232 fps capability
- Perfect for high-speed ball tracking
- Widely available, official Pi product

Both cameras use the same 5.077mm x 3.789mm sensor size and work identically once configured.

### Deprecated (Still Work, Not Recommended)

These older cameras work but aren't ideal for ball tracking:
- **Pi Camera v1.3** (OV5647) - Rolling shutter, motion blur issues
- **Pi Camera v2** (IMX219) - Rolling shutter, motion blur issues
- **Pi HQ Camera** (IMX477) - Expensive, overkill, still has rolling shutter

If you already have these, they'll work. But if buying new, get the Global Shutter camera.

### Not Supported

- **Pi Camera v3** (IMX708) - Newer sensor but lacks high FPS mode needed for ball tracking

## Lens Options

PiTrac supports standard M12 mount lenses:

**6mm M12 Lens** (Default)
- 60° field of view
- Best all-around choice
- Good balance of coverage and detail

**3.6mm M12 Lens** (Wide Angle)
- 90° field of view
- Useful for confined spaces
- May need adjusted calibration

**8mm or 12mm M12 Lenses**
- Narrower field of view
- Experimental support
- Check configuration options before buying

The 6mm lens is what most enclosures are designed for. Use it unless you have a specific reason not to.

## Dual Camera Setup

### Camera 1 (Tee Camera)
- Watches the ball at address
- Usually mounted horizontally or slightly angled
- Captures ball at impact and initial flight
- Calibration takes ~30 seconds

### Camera 2 (Flight Camera)
- Positioned at an offset from Camera 1
- Tracks ball during flight
- Provides depth/distance data
- Calibration takes 90-120 seconds (yes, really - it's a two-process workflow in single-Pi mode)

You can run with just Camera 1 for basic ball speed and launch angle, but you need both cameras for full 3D tracking and spin detection.

## Physical Installation

Cameras mount in the enclosure via:
- CSI ribbon cables to Pi (CAM0 and CAM1 ports)
- 3D printed mounts (hardware section has STL files)
- Precise positioning is critical - calibration corrects for small variations, but get as close as you can to the reference design

Make sure:
- Cameras have clear view of ball position
- No obstructions in camera view
- Cables aren't too tight or pulling on connectors
- Lenses are focused (manual focus ring on M12 lenses)

## Camera Detection

PiTrac auto-detects cameras if your Pi is configured correctly.

Required in `/boot/firmware/config.txt` (Pi 5) or `/boot/config.txt` (Pi 4):
```
camera_auto_detect=1
```

Test detection:
```bash
# Pi 5
rpicam-hello --list-cameras

# Pi 4
libcamera-hello --list-cameras
```

You should see 2 cameras listed. If not, check connections and boot config.

The web interface has an "Auto Detect" button that identifies your cameras and sets the correct types automatically. Use it.

## Configuration

Camera settings are managed in the web interface under Configuration → Cameras:

**Critical Settings**:
- **Camera 1 Type / Camera 2 Type** - Must match your actual hardware (use Auto Detect)
- **Lens Choice** - 6mm or 3.6mm
- **Camera Gain** - Controls brightness (higher = brighter but noisier)

**Advanced Settings**:
- Exposure times
- Contrast
- Search center coordinates (where to look for ball)
- Calibration matrices (set by calibration wizard, don't touch manually)

Most settings have good defaults. The main things you'll adjust are camera types (once) and gain (as needed for your lighting).

## Calibration

Cameras must be calibrated before PiTrac can accurately measure ball flight. Calibration determines:
- **Focal length** - Specific to your lens and camera
- **Camera angles** - How each camera is tilted/positioned
- **Distortion corrections** - Lens aberrations

See the [Auto-Calibration]({% link camera/auto-calibration.md %}) guide for the recommended calibration method. It's a 4-step wizard that takes about 3 minutes total.

For historical reference or troubleshooting, see [Manual Calibration]({% link camera/camera-calibration.md %}) (legacy method, not needed for normal use).

## Troubleshooting

**Cameras not detected?**
- Check camera_auto_detect=1 in boot config
- Verify CSI cable connections
- Reboot after connecting cameras
- Try rpicam-hello --list-cameras to see what Pi sees

**Images too dark?**
- Increase camera gain in Configuration
- Check strobe power
- Verify strobes are firing (you'll hear them click)

**Images too bright/washed out?**
- Decrease camera gain
- Reduce strobe intensity if adjustable

**Ball detection fails?**
- Check focus (twist M12 lens slightly)
- Adjust search center coordinates
- Try different gain settings
- Make sure ball is actually in frame (use Testing Tools → Capture Still Image)

**One camera works, other doesn't?**
- Verify both camera types are set correctly
- Camera 2 needs Camera 1 to be working first in single-Pi mode
- Check if you actually need Camera 2 (some setups work fine with just Camera 1)

For detailed troubleshooting, see [Camera Troubleshooting]({% link troubleshooting.md %}).

## Hardware Considerations

**Pi 4 vs Pi 5**:
- Both work fine
- Pi 5 is faster, handles dual cameras better
- Boot config location differs (see above)
- GPIO chip numbers differ (PiTrac handles this automatically)

**Lighting**:
- IR strobes are essential for freezing ball motion
- Higher strobe power = better image quality = lower gain needed
- Good lighting >> high camera gain for image quality

**Cables**:
- Use the shortest CSI cables that work for your enclosure
- Longer cables can cause signal degradation
- Pi 5 can handle longer cables better than Pi 4

## Next Steps

1. **Install cameras** in enclosure
2. **Test detection** with rpicam-hello or Auto Detect button
3. **Set camera types** in Configuration
4. **Run calibration** wizard (Auto-Calibration)
5. **Test ball detection** in Testing Tools
6. **Start hitting balls**

The calibration wizard is the most important step - don't skip it. Without calibration, speed/angle measurements will be completely wrong.
