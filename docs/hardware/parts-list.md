---
title: Parts List
layout: default
nav_order: 1
parent: Hardware
description: Complete parts list and shopping guide for building your PiTrac DIY golf launch monitor, including Raspberry Pi, cameras, electronics, and 3D printing materials.
keywords: PiTrac parts list, DIY golf monitor components, raspberry pi 5, global shutter camera, 3D printing materials, electronics shopping
---

# PiTrac DIY Parts List

This document provides a comprehensive list of all components needed to build a PiTrac launch monitor.

{: .highlight }
📋 **Before ordering parts**, check the [Roadmap](../getting-started/roadmap.md) to understand which version you should build and any recent updates to hardware requirements.

## Computing Hardware

| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 1 | [Raspberry Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/) and power supply, 4 GB minimum, 8 GB recommended | Getting a bundled kit with the Pi and power supply can be economical if you don't have Pi-related components yet |
| 1 | [DEPRECATED] ~~Raspberry Pi 4 Model B~~ | The current version of PiTrac requires only a single Pi 5. The "Single-Pi" version is relatively new but appears to be working well |
| 1 | [Pimoroni NVMe Base for Raspberry Pi 5 - PIM699](https://www.adafruit.com/product/5845) (optional, but recommended) | Allows use of NVMe memory drive instead of MicroSD card. Alternative: [Geekworm M901](https://www.amazon.com/gp/product/B0CQ4D2C9S/) |
| 1 | [NVMe SSD drive](https://www.amazon.com/gp/product/B0BGFRZDTB/) (optional, but recommended) | M.2 M key edge connector, 2230/2242 form factors. Recommend at least 256 GB capacity |
| 2 | Micro SD cards (64GB recommended) | To bootstrap the Pi's |
| 1 | Active Cooler fan for Pi 5 (optional) | Recommended if doing large compiles on the Pi |

## Camera and Lighting Hardware

| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 2 | [Raspberry Pi Global Shutter Camera - CS Lens Mount](https://www.adafruit.com/product/5702) **OR** [Innomaker GS Camera Module with IMX296 Mono Sensor](https://www.amazon.com/gp/product/B093BY2TK2/) | Innomaker is easier to work with (no soldering required). Pi cameras don't include lenses |
| 1 | [Pi 5 FPC Camera Cable - 22-pin 0.5mm to 15-pin 1mm - 300mm](https://www.adafruit.com/product/5819) | Conversion cable for Pi 5's smaller CSI ports |
| 1 | [200mm Flex Cable for Pi 4](https://www.adafruit.com/product/2087) | May come with cameras |
| 2 | [6mm 3MP Wide Angle Lens](https://www.adafruit.com/product/4563) | For Pi GS cameras (which come without lenses) |
| 1 | [1" x 1" IR Longpass Filter](https://www.edmundoptics.com/p/1quot-x-1quot-optical-cast-plastic-ir-longpass-filter/5421/) (1.5mm thickness) | Blocks visible light below ~730nm. **Important:** Must allow 730nm IR light through |
| 1 | [60/120 Degree LED Lens - 44mm + Reflector](https://www.amazon.com/dp/B09XK7QTV5) | Focuses the infrared light. Consider getting both 60° and 120° lenses |
| 1 | [100W COB IR LED Chip - 730nm](https://www.amazon.com/dp/B09DNRT2R4) | **Must be 730nm** for proper IR strobe operation |
| 1 | [USB COB LED Strip Lights - 6.56FT](https://www.amazon.com/Aclorol-Powered-Daylight-Flexible-Backlight/dp/B0D1FYV3LM/) | For lighting the teed-up ball. **Must produce NO infrared light** |

## Power Components

| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 1 | [LED Driver 18-39V 3000mA 120W](https://www.aliexpress.com/i/2251832563139779.html) (RECOMMENDED) **OR** [12-26V 3600mA Driver](https://www.aliexpress.us/item/2251832563139779.html) | Driver for LED strobe array |
| 1 | 5V USB-B power supply with micro USB connector | For Connector Board - provides isolated power plane |
| 1 | Power Strip (10" or less) | Compact enough to fit in enclosure, with power filtering |

## Hardware - Bolts and Nuts

### Base Enclosure Hardware
| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 4 | M4 x 12mm screws | LED power supply hold-downs |
| 4 | M2.5 x 12mm bolts + nuts | Pi board bolt-down |
| 4 | M2.5 x 10mm bolts (3) + M2.5 x 12mm bolt (1) | Pi board bolt-down |
| 8 | M2.5 x 16mm bolts + nuts | Pi camera attachment bolts |
| 6 | M4 x 12mm bolts + nuts | Pi camera gimbal attachment |
| 2 | M5 x 12mm bolts + nuts | Pi camera swivel mount |
| 6 | M3 x 16mm bolts + nuts | Horizontal center-side body attachment |
| 18 | M3 x 10mm self-tapping screws | Floor hold-down screws |
| 8 | M3 x 8mm screws | LED and lens hold-down screws |

### Version 2 Enclosure Hardware (Work in Progress)
| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 2 | M3 x 8mm self-tapping screws | AC power inlet plug |
| 2 | M3 x 8mm self-tapping screws | Base box end-cap |
| 12 | M2 x 6mm self-tapping screws | Tower back/front plate alignment |
| 4 | M4 x 12mm self-tapping screws | Tower feet to base box |
| 6-12 | M2.5 x 8mm self-tapping screws | Compute board to backplane |

{: .note }
**Hardware Note:** Stainless steel screws are stronger than black carbon steel and recommended, especially with PLA material. See [stainless steel assortment kit](https://www.googleadservices.com/pagead/aclk?sa=L&ai=DChcSEwiLuLi4w9eJAxW8Ka0GHe7XF-QYABALGgJwdg) for bulk purchasing.

## Connector Board Components

| Qty | Reference | Value | Description | Notes |
|-----|-----------|--------|-------------|-------|
| 1 | J3 | ~ | [USB B Micro Female Pinboard](https://www.amazon.com/Pinboard-MELIFE-Interface-Adapter-Breakout/dp/B07W6T97HZ/) | Includes pin headers |
| 2 | R1, R4 | 330Ω | Resistor | [Assortment kit](https://www.amazon.com/gp/product/B003UC4FSS/) |
| 2 | R2, R3 | 270Ω | Resistor | Axial DIN0207 |
| 1 | Sys1_Conn1 | ~ | 4 Pin Header | 2.54mm vertical |
| 1 | Sys2_Conn1 | ~ | 3 Pin Header | 2.54mm vertical |
| 2 | U2, U3 | H11L1 | [Optocoupler](https://www.amazon.com/10PCS-H11L1M-Photoelectric-Coupler-Optocoupler/dp/B09PK3V339) | **Must be DIP-6 style, not surface mount** |
| 1 | U4 | 74HC04 | Hex Inverter | DIP-14 with socket |
| 1 | U5 | ~ | [Dual MOS Driver Module](https://www.amazon.com/Anmbest-High-Power-Adjustment-Electronic-Brightness/dp/B07NWD8W26/) | For LED switching |

### Optional Connector Board Parts
| Qty | Item | Purpose |
|-----|------|---------|
| 2 | 6-pin DIP socket | For optocouplers ([assortment kit](https://www.amazon.com/dp/B01GOLSUAU)) |
| 1 | 14-pin DIP socket | For hex inverter |
| 1 | M2.5 bolt + nut | USB connector mechanical securing |

## Miscellaneous Parts

| Quantity | Hardware | Purpose/Notes |
|----------|----------|---------------|
| 2 | [Power Plugs](https://www.amazon.com/Ideal-Industries-30-102-Power-72427/dp/B01LYF1WV9/) | Easy connect/disconnects |
| N/A | [3 and 4-pin ribbon cables](https://www.amazon.com/Kidisoii-Dupont-Connector-Pre-Crimped-5P-10CM/dp/B0CCV1HVM9/) | GPIO to Connector Board wiring |
| 1 | 15.5cm x 24cm plexiglass window | **Must not block IR!** Protects cameras from ball strikes |

## Version 2 Additional Components (Work in Progress)

| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 1 | [AC Power Inlet C14 with Fuse](https://www.amazon.com/IEC320-Socket-Holder-Module-Connector/dp/B081ZFHRGW/) | For base box power input |
| 1 | [Mean Well LRS-150-24 PSU](https://www.amazon.com/dp/B07GTY6R4H) | 150W switching power supply (24V 6.5A) |
| 5 | [Ring Wire Connectors](https://www.amazon.com/dp/B0CYM3J44Q) | PSU wire connections |

{: .warning }
**Important Notes:**
- IR filter must allow 730nm light through (not 850nm filters)
- LED strip must produce NO infrared light
- Verify all IR components are compatible with 730nm wavelength
- Check [Roadmap](../getting-started/roadmap.md) before ordering to ensure you're building the right version