#!/bin/bash
# Diagnostic script for PiTrac GPIO and camera resource conflicts
# Run this to check what's holding resources

echo "=== PiTrac GPIO/Camera Resource Diagnostic ==="
echo "Date: $(date)"
echo ""

# Check for running PiTrac processes
echo "1. Checking for PiTrac processes..."
ps aux | grep -E "pitrac|lm_main" | grep -v grep
echo ""

# Check camera device usage
echo "2. Checking camera device usage..."
if [ -e /dev/video0 ]; then
    echo "   /dev/video0:"
    sudo lsof /dev/video0 2>/dev/null || echo "   No processes using /dev/video0"
fi
if [ -e /dev/video1 ]; then
    echo "   /dev/video1:"
    sudo lsof /dev/video1 2>/dev/null || echo "   No processes using /dev/video1"
fi
echo ""

# List video devices
echo "3. Available video devices:"
v4l2-ctl --list-devices 2>/dev/null || echo "   v4l2-ctl not available"
echo ""

# Check GPIO chip usage
echo "4. Checking GPIO usage..."
echo "   GPIO chips:"
ls -la /dev/gpiochip* 2>/dev/null || echo "   No GPIO chips found"
echo ""

# Check for lgpio processes
echo "5. Checking lgpio usage..."
sudo lsof 2>/dev/null | grep -i gpio | head -20
echo ""

# Check SPI device usage
echo "6. Checking SPI device usage..."
if [ -e /dev/spidev0.0 ]; then
    echo "   /dev/spidev0.0:"
    sudo lsof /dev/spidev0.0 2>/dev/null || echo "   No processes using /dev/spidev0.0"
fi
if [ -e /dev/spidev0.1 ]; then
    echo "   /dev/spidev0.1:"
    sudo lsof /dev/spidev0.1 2>/dev/null || echo "   No processes using /dev/spidev0.1"
fi
echo ""

# Check systemd services
echo "7. Checking systemd services..."
systemctl status pitrac 2>/dev/null | head -10 || echo "   pitrac service not found"
echo ""

# Check for stale lock files
echo "8. Checking for lock/pid files..."
ls -la /var/run/pitrac/ 2>/dev/null || echo "   No /var/run/pitrac directory"
ls -la ~/.pitrac/run/ 2>/dev/null || echo "   No ~/.pitrac/run directory"
echo ""

# Check libcamera status
echo "9. Testing libcamera..."
timeout 2 rpicam-still --list-cameras 2>&1 | head -20 || echo "   libcamera test failed/timed out"
echo ""

echo "=== Diagnostic Complete ==="
echo ""
echo "Common issues found:"
echo "- GPIO pin 25 (BCM) not released after PiTrac exits"
echo "- SPI handle not properly closed"
echo "- Camera resources locked by libcamera"
echo ""
echo "To fix, run: ./cleanup_pitrac_resources.sh"