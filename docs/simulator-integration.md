---
title: Simulator Integration
layout: default
nav_order: 5
---

# Connecting to Golf Simulators

PiTrac can send shot data to E6 Connect, GSPro, and TruGolf simulators. The process is pretty straightforward - you just need to tell PiTrac where your simulator software is running on your network.

## Network Setup

First things first: your Raspberry Pi and the PC running your simulator software need to be on the same network and able to talk to each other.

### Find Your Simulator PC's IP Address

On the computer running your simulator:

**Windows:**
1. Open Command Prompt
2. Type `ipconfig`
3. Look for "IPv4 Address" under your network adapter
4. Should look like `192.168.1.100` or similar

**Mac:**
1. Open Terminal
2. Type `ifconfig`
3. Look for "inet" under your network adapter (en0 or en1)
4. Should look like `192.168.1.100` or similar

Write this down - you'll need it.

### Test Network Connectivity

From your Raspberry Pi, make sure you can reach the simulator PC:

```bash
ping 192.168.1.100
```

(Replace with your actual IP)

You should see replies. If you get "Destination Host Unreachable" or timeouts, you have a network problem to fix first. Check:
- Both devices on the same WiFi/Ethernet network?
- Firewall blocking pings?
- Router doing weird stuff?

## E6 Connect / TruGolf

### On the Raspberry Pi

1. Open the PiTrac web interface (`http://{PI-IP}:8080`)
2. Navigate to Configuration (3-dot menu → Configuration)
3. Find the **Simulators** category
4. Look for the **E6** section:
   - **kE6ConnectAddress** - Enter your simulator PC's IP (e.g., `192.168.1.100`)
   - **kE6ConnectPort** - Should be `2483` (this is standard for E6)
   - **kE6InterMessageDelayMs** - Leave at `50` unless you have issues
5. Click **Save Changes**

### On E6 Connect

1. Launch E6 Connect
2. Click the **Settings** (gear icon)
3. Select **Simulator / Tracking System**
4. Click **Configure** next to **TruSimAPI**
5. Verify the settings:
   - **IP Address** - Should be your simulator PC's IP
   - **Port** - Should be `2483`
6. Make sure **TruSimAPI** is selected as the active tracking system

### Testing the Connection

1. On the Pi, make sure PiTrac LM is stopped
2. On the simulator PC, start E6 Connect
3. In E6, select **Practice** and start a session
4. On the Pi, run the test script:
   ```bash
   cd $PITRAC_ROOT/ImageProcessing/RunScripts
   ./runTestExternalSimMessage.sh
   ```

This sends a test shot to E6. If it works, you'll see the ball fly in E6.

If it doesn't work:
- Check the logs on both the Pi and E6
- Turn on trace logging: `./runTestExternalSimMessage.sh --logging_level=trace`
- Make sure E6 is actually waiting for a shot (in a practice round, etc.)
- Verify the IP and port settings again

## GSPro

### On the Raspberry Pi

1. Open the PiTrac web interface
2. Navigate to Configuration
3. Find the **Simulators** category
4. Look for the **GSPro** section:
   - **kGSProHostAddress** - Enter your simulator PC's IP (e.g., `192.168.1.100`)
   - **kGSProPort** - Should be `921` (standard for GSPro)
5. Click **Save Changes**

### On GSPro

GSPro should automatically detect PiTrac when it's running. Make sure:
- GSPro is set to listen for external launch monitors
- The connection port is `921`
- No firewall is blocking port `921`

Check GSPro's launch monitor settings - it should show PiTrac as connected once you start hitting balls.

### Testing

Start GSPro, then from the Pi run:
```bash
cd $PITRAC_ROOT/ImageProcessing/RunScripts
./runTestGsProServer.sh
```

This sends a test shot to GSPro. Watch GSPro to see if it receives and displays the shot data.

## General Connection Tips

**Start Order:** It usually works best to:
1. Start your simulator software first
2. Get it into practice/play mode
3. Then start PiTrac LM
4. Hit balls

**Firewall:** Make sure your simulator PC's firewall isn't blocking incoming connections on the relevant ports (2483 for E6, 921 for GSPro).

**Network:** Wired Ethernet is more reliable than WiFi. If you're having weird intermittent issues, try a wired connection.

**Multiple Simulators:** You can configure multiple simulators in PiTrac. It will send shot data to all of them. Whether that actually works depends on the simulator software - some don't like sharing.

## Troubleshooting

**"Connection Refused" or "No Route to Host":**
- Double-check the IP address
- Make sure simulator software is actually running
- Check firewall settings
- Try pinging the IP from the Pi

**Simulator doesn't receive shots:**
- Verify the port numbers
- Check that simulator is in "waiting for shot" mode
- Look at logs on both ends
- Try the test scripts to isolate the problem

**Shots received but values are wrong:**
- This is probably not a connection issue
- Check your calibration
- Verify camera settings
- Look at troubleshooting section for shot detection issues

**Connection works sometimes:**
- Usually a network issue (WiFi interference, router problems)
- Try wired connection
- Check for IP address changes (use static IPs if possible)
- Look for other network traffic causing problems

Still stuck? Head to the Discord with:
- What simulator you're using
- Error messages from logs
- Results of the test scripts
- Network setup details (WiFi/wired, router type, etc.)

We'll get you sorted out.
