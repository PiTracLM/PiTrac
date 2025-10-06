---
title: Startup Guide
layout: default
nav_order: 2
parent: Software
---

# PiTrac Testing and Start-Up Documentation

If you are at this point, you should have PiTrac compiled on both Pi's, your enclosure built, and your cameras calibrated. If that's not quite done yet, see the [Automated Setup]({% link software/automated-setup.md %}) for the quickest path, or the [Raspberry Pi Setup]({% link software/pi-setup.md %}) for manual installation.

## Environment Setup

### Required Environment Variables

Ensure each account you will run PiTrac from has the following environment variables set. Add these to your `.zshrc` file in your user's root directory:

```bash
export PITRAC_ROOT=/Dev/PiTrac/Software
# Note: The ~ symbol is only expanded by the shell, so 
# may not work if pulled directly into PiTrac code. However,
# these variables are expanded before being injected into PiTrac
# via command-line parameters
export PITRAC_ROOT=/mnt/PiTracShare/Dev/PiTrac/Software/LMSourceCode
export PITRAC_BASE_IMAGE_LOGGING_DIR=~/LM_Shares/Images/
export PITRAC_WEBSERVER_SHARE_DIR=~/LM_Shares/WebShare/
export PITRAC_MSG_BROKER_FULL_ADDRESS=tcp://10.0.0.41:61616

# Only uncomment and set the following if connecting to the
# respective golf sim (e.g., E6/TruGolf, GSPro, etc.)
#export PITRAC_E6_HOST_ADDRESS=10.0.0.29
#export PITRAC_GSPRO_HOST_ADDRESS=10.0.0.29
```

### Configuration File

Ensure the `golf_sim_config.json` file is correctly set up. If unsure, follow the [Configuration File documentation]({% link software/configuration.md %}).

### Basic Executable Test

Check that the executable runs by itself:

```bash
cd $PITRAC_ROOT/ImageProcessing
build/pitrac_lm --help
```

The executable should show the command-line parameters.

## Hardware Testing

### Strobe Light and Camera Triggering Test

Problems can sometimes exist in the pathway from Pi 1 to Pi 2 Camera and the Strobe Assembly through the Connector Board. Perform these initial checks:

1. **Position PiTrac** so you can see into the IR LEDs through the LED lens (small array of square LEDs should be visible)

2. **Run strobe-light test:**
   ```bash
   cd $PITRAC_ROOT/ImageProcessing
   ./RunScripts/runPulseTest.sh
   ```

3. **Observe the test:** The script sends short "on" pulses to the LED strobe light. Due to the IR wavelengths used, you should see very short groups of dark-reddish pulses in the LED lens.

   {: .warning }
   **Safety:** Look at the LED from at least a couple feet away, especially with higher-power LEDs.

4. **Troubleshoot if no pulsing:**
   - Check runPulseTest script output
   - Verify connections from Pi 1 (top of LM) to Connector Board
   - If you can't see red pulses:
     - Check if small red LED on Connector Board is pulsing
     - If Connector Board LED pulses but no strobe light pulses:
       - Verify power supply to Connector Board connection
       - Check wiring from Connector Board output to LED strobe assembly

5. **Stop the test:** Hit `Ctrl-C` to stop the pulse test

   {: .danger }
   **WARNING:** Double-check after stopping that the LED is OFF (showing no red color)!

### Camera 2 Shutter Triggering Test

When running normally, Pi 1 triggers Camera 2's shutter. Confirm this signal pathway works:

1. **Set external triggering mode:**
   ```bash
   # On Pi 2:
   cd $PITRAC_ROOT/ImageProcessing
   sudo ./CameraTools/setCameraTriggerExternal.sh
   ```

2. **Test camera response:**
   ```bash
   rpicam-hello
   ./RunScripts/runCam2Still.sh
   ```

   Normally, the camera won't take a picture because it's waiting for Pi 1's signal. The `rpicam-hello` should hang and do nothing while external triggering is set.

3. **Trigger from Pi 1:**
   ```bash
   # On Pi 1:
   ./RunScripts/runPulseTest.sh
   ```

   As soon as Pi 1 starts sending pulses to Camera 2 (and LED strobe), Camera 2 should take a picture. The resulting picture will likely be dark if you have the IR filter installed.

4. **Return to internal triggering:**
   ```bash
   # On Pi 2:
   $PITRAC_ROOT/CameraTools/setCameraTriggerInternal.sh
   ```

## Full System Startup

### Running PiTrac

For the easiest way to run PiTrac, see the [Running PiTrac]({% link software/running-pitrac.md %}) guide which covers the menu system, background processes, and all runtime options.

To run PiTrac manually, start both camera scripts:

1. **Start Pi 2 first:**
   ```bash
   # On Pi 2:
   ./RunScripts/runCam2.sh
   ```

2. **Then start Pi 1:**
   ```bash
   # On Pi 1:
   ./RunScripts/runCam1.sh
   ```

Start Pi 2 executable first so it's ready to take pictures as soon as Pi 1 determines a ball has appeared.

### Logging Levels

- Run executables with `--logging_level=info` or higher (e.g., `warning`)
- Setting to `DEBUG` or `TRACE` may slow the system so much it won't reliably catch golf ball flight
- However, trace-level information is often useful for debugging

### Troubleshooting

For problems, see the [Troubleshooting Guide]({% link troubleshooting.md %}).

## Performance Tips

- **Memory:** Ensure adequate swap space for compilation (see [Pi Setup Guide]({% link software/pi-setup.md %}))
- **Network:** Use wired Ethernet for stability during operation
- **Storage:** NVMe drives significantly improve performance over SD cards
- **Cooling:** Active cooling helps maintain consistent performance during long sessions

## Next Steps

Once the system is running reliably:

1. **Calibration:** Fine-tune camera calibration if needed ([Camera Calibration]({% link camera/camera-calibration.md %}))
2. **Integration:** Set up simulator connections ([Integration]({% link simulator-integration.md %}))
3. **Optimization:** Adjust settings for your specific setup and environment
