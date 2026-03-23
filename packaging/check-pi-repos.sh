#!/bin/bash
# Check what packages are available in various ARM64 repositories
# that we're currently building from source
#
# Repositories checked:
# 1. Standard Debian Bookworm
# 2. Raspberry Pi official repository
# 3. Ubuntu ports (for comparison)

set -e

echo "Checking various ARM64 repositories for our dependencies..."
echo "=============================================="

docker run --rm --platform=linux/arm64 debian:bookworm-slim bash -c '
    # Setup
    apt-get update -qq && apt-get install -y ca-certificates wget gnupg >/dev/null 2>&1
    
    # Add Raspberry Pi repository with proper GPG key
    echo "Adding Raspberry Pi repository..."
    echo "deb http://archive.raspberrypi.org/debian/ bookworm main" > /etc/apt/sources.list.d/raspi.list
    
    # Import the GPG key properly
    wget -qO - http://archive.raspberrypi.org/debian/raspberrypi.gpg.key | gpg --dearmor > /usr/share/keyrings/raspberrypi-archive-keyring.gpg
    echo "deb [signed-by=/usr/share/keyrings/raspberrypi-archive-keyring.gpg] http://archive.raspberrypi.org/debian/ bookworm main" > /etc/apt/sources.list.d/raspi.list
    
    apt-get update -qq
    
    echo "=== Checking ActiveMQ-CPP ==="
    echo "Currently building: 3.9.5 from source"
    apt-cache search activemq | grep -i cpp || echo "Not found in Pi repos"
    echo
    
    echo "=== Checking ActiveMQ Broker ==="
    apt-cache search activemq | grep -v cpp || echo "Not found in repos"
    echo
    
    echo "=== Checking TomEE ==="
    apt-cache search tomee || echo "Not found in repos"
    echo
    
    echo "=== Checking Tomcat ==="
    apt-cache search tomcat | head -5 || echo "Not found in repos"
    echo
    
    echo "=== Checking lgpio ==="
    echo "Currently building: 0.2.2 from source"
    apt-cache search lgpio || echo "Not found in Pi repos"
    echo "Details from Pi repos:"
    apt-cache show liblgpio-dev 2>/dev/null | grep -E "^(Package|Version|Description)" | head -6
    echo
    
    echo "=== Checking msgpack-cxx ==="
    echo "Currently building: 6.1.1 from source"
    apt-cache search msgpack | grep -E "(c\+\+|cxx|cpp)" || echo "Not found in Pi repos"
    echo "Details from Debian repos:"
    apt-cache show libmsgpack-cxx-dev 2>/dev/null | grep -E "^(Package|Version|Description)" | head -6
    echo
    
    echo "=== Checking OpenCV ==="
    echo "Currently building: 4.11.0 from source"
    apt-cache policy libopencv-dev 2>/dev/null | head -10 || echo "Not found in Pi repos"
    
    # Also check libcamera versions
    echo
    echo "=== Checking libcamera (system package we use) ==="
    apt-cache policy libcamera0* 2>/dev/null | grep -E "^(libcamera|  Candidate)" | head -10
    
    echo
    echo "=== Summary of available versions ==="
    echo "ActiveMQ-CPP: Not available (need 3.9.5)"
    echo -n "lgpio: "
    apt-cache policy liblgpio-dev 2>/dev/null | grep "Candidate:" | awk "{print \$2}" || echo "Not available"
    echo "       (need 0.2.2, checking if Pi repo version is compatible)"
    echo -n "msgpack-cxx: "
    apt-cache policy libmsgpack-cxx-dev 2>/dev/null | grep "Candidate:" | awk "{print \$2}" || echo "Not available"
    echo "       (building 6.1.1, Debian has 4.1.3)"
    echo -n "OpenCV: "
    apt-cache policy libopencv-dev 2>/dev/null | grep "Candidate:" | awk "{print \$2}" || echo "Not available"
    echo "       (need >= 4.9.0 for YOLO v8n support)"
'

echo
echo "=============================================="
echo "Checking Ubuntu 22.04 LTS ARM64 packages for comparison..."
echo "=============================================="

docker run --rm --platform=linux/arm64 ubuntu:22.04 bash -c '
    apt-get update -qq >/dev/null 2>&1
    
    echo "=== Ubuntu 22.04 (Jammy) Package Versions ==="
    echo -n "OpenCV: "
    apt-cache policy libopencv-dev 2>/dev/null | grep "Candidate:" | awk "{print \$2}" || echo "Not available"
    echo -n "msgpack-cxx: "
    apt-cache policy libmsgpack-cxx-dev 2>/dev/null | grep "Candidate:" | awk "{print \$2}" || echo "Not available"
    echo -n "lgpio: "
    apt-cache search lgpio 2>/dev/null | head -1 || echo "Not available"
'

echo
echo "=============================================="
echo "Checking Ubuntu 24.04 LTS ARM64 packages for comparison..."
echo "=============================================="

docker run --rm --platform=linux/arm64 ubuntu:24.04 bash -c '
    apt-get update -qq >/dev/null 2>&1
    
    echo "=== Ubuntu 24.04 (Noble) Package Versions ==="
    echo -n "OpenCV: "
    apt-cache policy libopencv-dev 2>/dev/null | grep "Candidate:" | awk "{print \$2}" || echo "Not available"
    echo -n "msgpack-cxx: "
    apt-cache policy libmsgpack-cxx-dev 2>/dev/null | grep "Candidate:" | awk "{print \$2}" || echo "Not available"
    echo -n "lgpio: "
    apt-cache search lgpio 2>/dev/null | head -1 || echo "Not available"
'

echo
echo "=============================================="
echo "Summary: Package availability across repositories"
echo "=============================================="
echo
echo "Packages we MUST build from source:"
echo "- ActiveMQ-CPP 3.9.5 (not in any repos)"
echo "- OpenCV 4.11.0 (need >= 4.9.0 for YOLO v8n, repos have <= 4.8)"
echo
echo "Packages potentially available from repos:"
echo "- lgpio: Available in Raspberry Pi repos (check version compatibility)"
echo "- msgpack-cxx: Available in Debian (4.1.3), need to verify if compatible with 6.1.1 features"
echo
echo "Note: We target Raspberry Pi OS Bookworm, so Debian/RaspberryPi repos take precedence"