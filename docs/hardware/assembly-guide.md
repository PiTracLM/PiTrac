---
title: Assembly Guide
layout: default
nav_order: 2
parent: Hardware
description: Complete step-by-step guide for assembling your PiTrac DIY golf launch monitor, including 3D printed parts, camera mounting, and electronics installation.
keywords: PiTrac assembly, DIY launch monitor build, 3D printed enclosure, camera installation, raspberry pi mounting
toc: true
---

# PiTrac Assembly Guide

This document explains the process of building a typical PiTrac DIY Launch Monitor (LM). This process includes the assembly of all physical components, including the 3D-printed parts and hardware such as cameras and Raspberry Pi computers.

{: .note }
**Important:** This document refers to the calibration steps for the camera sub-system, which should be consulted prior to building the enclosure. Calibration is most easily done *as the LM is being assembled* because it allows easier access to cameras and camera mounts before the LM is completed. See [Camera Calibration]({% link camera/camera-calibration.md %}) for details.

## Overview

The LM is not a simple, complete kit that just requires attaching Part A to Part B. Instead, it's a general design that we hope builders will customize, adapt and improve depending on their goals and skills. The particular parts referenced here may not even be available when you build your LM. This document is a guide for building what is expected to be a 'typical' PiTrac LM.

## Conventions

- **Direction References:** Left/right/back/front refer to the LM as it faces the user, with the camera side toward the builder
- **Pi-side:** Right half where the Pi's, cameras, and Connector Board are placed
- **Power side:** Left side where power adapters and cables are contained
- **Terminology:** 3D parts use a house analogy - curved outside parts are "walls," flat horizontal parts are "floors"

{: .note }
**Note:** Enclosure terminology diagram referenced in original documentation is missing from image files.

## Tools Needed

### Essential Tools
1. M2.5, M3, M4, and M5 hex wrenches (both right-angle and straight)
2. Needle-nose pliers
3. Medium-sized Phillips screwdriver (at least 6" length)
4. Soldering tools
5. Wiring tools (wire strippers, clippers, etc.)

### Optional Tools
1. Magnifying, lighted headband and/or magnifying desk light
2. Dremel tool for trimming parts if needed
3. Soft/hard rubber hammer or mallet

## Parts and Materials

1. See the [Parts List]({% link hardware/parts-list.md %}) for complete component list
2. 3D-printed enclosure parts
3. Solder, flux, flux-cleaner
4. Wires (stereo-speaker wire recommended for higher-voltage portions)

### Optional Materials
- Sandpaper and sanding block
- Non-IR-reflective cloth material (black felt) for backdrop
- **Note:** Some black cloth appears white under IR light - test first

## Assembly Instructions

### 1. Printing the Enclosure Components

First, see the individual printing and assembly instructions for each 3D part. The 3D printing is a multi-day project due to part sizes.

{: .tip }
**Pro Tip:** Having hardware components in hand *before* printing allows you to customize part models to fit your specific components. For example, different LED power supplies may require different mounting holes.

### 2. Base Layer Assembly

The base layer is the foundation that touches the ground. It consists of two wall halves that connect in the middle, with floors inserted into mounting risers.

#### Wall Halves and Lower Component Bays

1. **Prepare Pi-side half-join:** Place two M3 nuts into the indents on the Pi-side half-join pads. Use temporary bolts from outside to pull nuts into indents if tight. Tape helps hold nuts in place.

2. **Install LED power supply:** Install before joining wall halves (it's heavy, so keep it low).
   - Place in right-hand half
   - Secure to floor risers with four M4 x 12mm screws
   - Use [recommended supply](https://www.aliexpress.us/item/2251832563139779.html) or similar
   - Add quick-connect to output wires with clear polarity markings
   - Input should have 3-prong plug for utility strip

3. **Install network switch:** If it fits and allows cable connections, place in Power-side bay now. Route cables:
   - One cable out the back of LM
   - Two cables to Pi's on right side

4. **Join base halves:** Connect using two pairs of M3 x 16mm bolts and nuts.

#### External (Tee-up) LED Lighting

Install LED lighting under the base layer overhang for Camera 1 visibility:

1. **Mount LED strips:** Using [recommended strips](https://www.amazon.com/Aclorol-Powered-Daylight-Flexible-Backlight/dp/B0D1FYV3LM/), create double brightness by using two layers
2. **Position first layer:** Tack down one end on left side, stick below overhang, push ~4 inches into right LED port
3. **Add second layer:** Stick second layer just below first for double LED rows

![LED Strip Installation](/assets/images/enclosure_assembly/enclosure_light_strip.png)
![LED Strip Layer 2](/assets/images/enclosure_assembly/enclosure_light_strip_2.png)

4. **Route power:** Pull USB power plug through left LED port, either to internal power strip or external power for independent control
5. **Secure strips:** Use zip-ties through small holes at base layer ends

![LED Strip 3](/assets/images/enclosure_assembly/enclosure_light_strip_3.png)
![LED Strip 4](/assets/images/enclosure_assembly/enclosure_light_strip_4.png)
![LED Strip 5](/assets/images/enclosure_assembly/enclosure_light_strip_5.png)

### 3. Camera 2 Assembly (Flight Camera)

Camera 2 is the bottom camera that captures the ball in flight and requires modifications.

#### Camera Modifications

{: .warning }
**WARNING:** These modifications void the camera warranty and are delicate operations that can damage the camera if performed incorrectly.

1. **Remove IR filter:** Use [official RPi instructions](https://www.raspberrypi.com/documentation/accessories/camera.html#filter-removal)

2. **Add external triggering:** Follow [RPi external trigger guide](https://github.com/raspberrypi/documentation/blob/develop/documentation/asciidoc/accessories/camera/external_trigger.adoc)
   - Resistor removal is particularly difficult - use magnifying light and steady hands
   - Practice on old surface-mount PCBs first
   - Use quick-connect wires with jumper pins for easier connection/disconnection

![Camera 2 Preparation](/assets/images/enclosure_assembly/enclosure_assembly_cam2_prep.png)

3. **Strain relief:** Add thick glue drop over wire/solder junction. Run wires down from camera top and pinch between camera and gimbal backplate.

#### Camera Mount Assembly

1. **Mount selection:** Use shorter mount for Camera 2 (doesn't need to point downward like Camera 1)
2. **Cable first:** Attach CSI cable *before* bolting camera to backplate - lock cable securely
3. **Threading:** Carefully thread CSI cable out back of mount without pinching

#### Add IR Filter

1. **Filter holder assembly:** Glue two parts together using registration wings for alignment

![Camera Filter](/assets/images/enclosure_assembly/camera_filter.png)

2. **Install filter:** Insert visible light-cut filter, attach holder to lens with opening at top

### 4. Pi-side Base Layer Floor

1. **Mount Camera 2:** Use M5 bolt and nut with CSI cable and trigger wires in place
2. **Mount Pi 4/5:** Use 4x M2.5 x 12mm bolts and nuts (nuts on bottom). Insert MicroSD card before mounting
3. **Connect camera CSI cable** to Pi
4. **Connect camera GND** to Pi pin 9
5. **Create Pi 2-to-Connector-Board ribbon cable:**
   - Fashion 3-wire harness with female plugs each end
   - Use black/red wires plus third wire for Camera 2 XTR
   - See wiring diagram reference below

![PCB Wiring Diagram](/assets/images/enclosure_assembly/pcb_wiring_diagram.png)

{: .note }
**Quality matters:** Low-quality wiring harnesses can cause random, difficult-to-diagnose problems. Ensure firm connections and make cables slightly longer than necessary.

Recommended: [Pre-crimped ribbon cable kit](https://www.amazon.com/Kidisoii-Dupont-Connector-Pre-Crimped-5P-10CM/dp/B0CCV1HVM9/)

![Pi Base Example](/assets/images/enclosure_assembly/pi_base_example.png)

### 5. Joining Base Layer Floor Halves

The two base floor halves join with an overlapping lap joint:

1. **Prepare power-side floor:** Remove printing supports and smooth lap joint area. Belt sanding may help.

![Belt Sanding](/assets/images/enclosure_assembly/belt_sanding.png)

2. **Pre-place screws:** Insert 6x M3 x 10mm screws a few turns into power-side holes and right-most Pi-side holes
3. **Join floors:** Slip power-side lap joint between camera base and Pi-side lap joint
4. **Install assembly:** Place joined floors into base walls, aligning lap joint in middle
5. **Secure floors:** Use M3 x 10mm screws into wall mounting pads

![Join Two Bottom Layers](/assets/images/enclosure_assembly/join_two_bottom_layers.png)

### 6. Complete Base Layer

1. **Route power cable:** Thread from power strip down through support and out base hole (tight fit - do first)
2. **Connect Pi power:** Route from Pi power supply to Pi board
3. **Connect network cable:** Route one network cable to Pi

![Base Layer One](/assets/images/enclosure_assembly/base_layer_one.png)
![Base Layer Two](/assets/images/enclosure_assembly/base_layer_two.png)

### 7. Prepare for Middle Layer

Route cables that will connect to Middle Layer components:
- Network cable from switch (to Pi 1 on middle layer upper floor)
- 3-wire harness from Pi 2 (to Connector Board on middle layer lower floor)
- LED strobe power cable (to LED strobe on middle layer lower floor)

Route these cables to the right side for easier middle layer installation.

### 8. Camera 2 Calibration

**Important:** Calibrate Camera 2 now while easily accessible. See [Camera Calibration Guide]({% link camera/camera-calibration.md %}).

## Middle Layer Assembly

The Middle Layer has two wall halves and two floors on the Pi side: lower floor (strobe LED and Connector Board) and upper floor (Pi 1 computer and Camera 1).

### 1. Build Strobe Assembly

See [LED Strobe Mount instructions](https://github.com/jamespilgrim/PiTrac/tree/main/3D%20Printed%20Parts/Enclosure%20Models/LED-Array-Strobe-Light-Mount).

### 2. Build Connector Board

See [Connector Board instructions](https://github.com/jamespilgrim/PiTrac/tree/main/Hardware/Connector%20Board).

{: .important }
**Critical:** Attach 12V IN/OUT wiring pairs and 4-wire harness to Connector Board screw terminals *before* mounting the floor. Access becomes difficult after upper floor installation.

### 3. Join Middle Layer Wall Halves

1. **Prepare nuts:** Place 4x M3 nuts in Pi-side half-join pad indents (use tape to hold)
2. **Join halves:** Use 4x pairs of M3 x 16mm bolts and nuts
3. **Check fit:** Sand half-join areas if gaps exist

### 4. Middle Layer Lower Floor Assembly

1. **Mount Connector Board:** Position with 4-pin header on left, writing upright. Secure with 4x M3 x 8/10mm self-tapping screws
2. **Mount strobe assembly:** Use M5 bolt from below through M5 nut in floor indent. Position LED power wires through side holders, face strobe straight out, tighten while accessible

### 5. Install Lower Floors

Mount both Pi-side and power-side lower floors to bottom of lower mounting pads using 4x M3 x 10mm self-tapping screws each.

![Middle Layer](/assets/images/enclosure_assembly/middle_layer.png)

Connect 5V micro-USB power to Connector Board from power adapter in left bay.

### 6. Camera 1 Assembly

Camera 1 is similar to Camera 2 but:
- Points downward steeply (uses taller riser mount)
- Uses visible light (no modifications needed)
- Requires different adapter cable for Pi 5

![Camera Mount Assembly](/assets/images/enclosure_assembly/camera_mount_assembly.png)

### 7. Pi-side Middle Layer Upper Floor

1. **Mount Camera 1:** Use M5 bolt and nut with CSI cable secured first
2. **Mount Pi 5:** Use 4x M2.5 x 12mm bolts and nuts (nuts on bottom). Install NVMe/SD card first. Consider active cooler for development use
3. **Connect camera CSI cable:** Use Pi 5-compatible cable (smaller connector format)

### 8. Install Upper Floor

1. **Guide cables:** Route 4-wire harness and second network cable to right as you place upper floor
2. **Mount floor:** Secure to side pads with 4x M3 x 10mm self-tapping screws
3. **Connect wiring:** 
   - 4-wire ribbon from Connector Board to Pi 1 GPIO (see wiring diagram)
   - Network cable to Pi 1
   - [Pi 5 power supply](https://www.amazon.com/CanaKit-PiSwitch-Raspberry-Power-Switch/dp/B07H125ZRL/) from power-side

### 9. Mount Middle Layer on Base Layer

1. **Prepare cables:** Ensure all base layer cables (3-wire harness, network, LED power) are positioned right
2. **Guide power components:** Route utility strip and power adapters up left side void
3. **Thread cables:** Guide base layer cables through right side space as middle layer is positioned

![Two Layers Together](/assets/images/enclosure_assembly/two_layers_together.png)

4. **Check clearances:** Ensure no wires will be pinched when layers connect
5. **Connect layers:** Insert middle layer click-tabs into base layer tabs. Set into back first, then pull front of base layer outward to help engagement

{: .warning }
**Safety:** Resist hitting layers together - middle layer top is sharp!

### 10. Connect Power Systems

1. **LED power supply to Connector Board:** Connect LED power output to "IN (12V)" terminals on Dual MOS switch
   
   {: .danger }
   **CRITICAL:** Double-check polarity! Reversed connection can destroy LED. Use clearly marked wires and keyed connectors when possible.

![MOS Switch Bottom](/assets/images/enclosure_assembly/mos_switch_board_bottom.png)
![MOS Switch Top](/assets/images/enclosure_assembly/mos_switch_board_top.png)

2. **Connector Board to LED Strobe:** Connect "OUT (12V)" terminals to LED strobe
   
   {: .danger }
   **WARNING:** Do NOT connect strobe directly to LED power supply output - this could melt the strobe or worse!

3. **Connect Pi 2 and Camera 2 harnesses:** Connect wiring from base layer to correct Connector Board header pins per schematic

### 11. Secure Layers

Secure base and middle layers with M3 x 6mm self-tapping screws in 4 interface holes. Consider dulling screw tips for safety.

## Camera 1 Calibration

Before installing the ceiling, calibrate Camera 1 using the [Camera Calibration Guide]({% link camera/camera-calibration.md %}).

## Top Wall (Ceiling) Assembly

1. **Connect ceiling halves:** Use 4x M3 bolts and nuts
2. **Install ceiling:** Snap onto middle layer top, secure with 4x M3 x 6mm self-tapping screws
3. **Dull screw tips** for safety

## Final Steps

### Add Front Window

Install 15.5cm x 24cm plexiglass anti-strike window by sliding into mounting slots. Window should be far enough from cameras/strobe to protect expensive components from ball strikes.

### PiTrac Badge/Logo

1. **Print components:** Back plate at full size, letters at 99% size for better fit
2. **Color scheme:** Use preferred colors (see reference image)

![PiTrac Logo](/assets/images/enclosure_assembly/pi_trac_logo.png)

3. **Assembly:** Use rubber hammer to tap characters into place. Friction holds parts, but cyanoacrylate glue provides permanent bond
4. **Mounting:** Use 2-sided tape (rough surfaces don't glue well)

## Next Steps

Once assembly is complete, proceed to the [Start-Up Documentation](../software/) for software installation and configuration.

---

## Troubleshooting Tips

- **Tight fits:** Use Dremel tool carefully to trim parts if needed
- **Stripped threads:** Consider brass threaded inserts for frequently-removed screws  
- **Cable management:** Leave extra length in all cables for easier assembly/disassembly
- **Component protection:** Always verify polarity before connecting power components