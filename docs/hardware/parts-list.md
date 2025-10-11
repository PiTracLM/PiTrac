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

**Only a single RPi5 is currently supported, however the path to stereoscopic vision currently requires two**

## Computing Hardware

| Quantity | Hardware | Purpose | Link |
|----------|----------|---------|------|
| 1-2 | Raspberry Pi 5 (8 GB recommended) | Main embedded computer | https://vilros.com/products/raspberry-pi-5?variant=40065551302750
| 1-2 | Active Cooler Kit | Required to keep temps low and timing consistent | https://a.co/d/dsl7saU
| 1-2 | MicroSD card (64 GB recommended) | For RPi5 filesystem | https://www.amazon.com/Amazon-Basics-microSDXC-Memory-Adapter/dp/B08TJTB8XS
| 1-2 | 1ft USB-C to USB-C Cable | For powering RPi5 from connector board | https://www.amazon.com/Anker-Charging-MacBook-Samsung-Nintendo/dp/B09H2DMR4K

## Camera and Lighting Hardware

| Quantity | Hardware | Purpose | Link |
|----------|----------|---------|------|
| 2 | Innomaker GS Camera Module with IMX296 Mono Sensor| RPi compatible GS cameras | https://www.inno-maker.com/product/cam-mipi296raw-trigger/
| 2 | Pi 5 FPC Camera Cable – 22-pin 0.5 mm to 15-pin 1 mm – 300 mm | Conversion cables for RPi5’s smaller CSI ports | https://www.adafruit.com/product/5819
| 1 | 6 mm 3 MP Wide Angle Lens | For GS camera | https://www.adafruit.com/product/4563
| 1 | 1″ × 1″ IR Longpass Filter | Must be a **longpass filter**, allowing >= 700nm light to pass | https://www.edmundoptics.com/p/1quot-x-1quot-optical-cast-plastic-ir-longpass-filter/5421/
| 1 | 60° LED Lens – 44 mm + Reflector | Focuses the infrared light | https://www.amazon.com/dp/B09XK7QTV5
| 1 | 100 W COB IR LED Chip – 730 nm| **Must be 730 nm** for proper IR strobe operation | https://www.amazon.com/dp/B09DNRT2R4
| 1 | USB COB LED Strip Lights – 6.56 ft | For lighting the teed-up ball. **Must produce no infrared light** | https://www.amazon.com/Aclorol-Powered-Daylight-Flexible-Backlight/dp/B0D1FYV3LM/

## Power Components

| Quantity | Hardware | Purpose | Link |
|----------|----------|---------|------|
| 1 | Meanwell LRS-75-5 | Provides plenty of power and forms base of unit | https://www.digikey.com/en/products/detail/mean-well-usa-inc/LRS-75-5/7705056
| 1 | AC Power Inlet C14 with Fuse | For base box power input | https://www.amazon.com/IEC320-Socket-Holder-Module-Connector/dp/B081ZFHRGW/
| 1 | 16ga Wire Spool Set | For Input Power + IR LED Wiring | https://www.amazon.com/Fermerry-Electric-Silicone-Cables-Stranded/dp/B089CPH72F

## Connector Board
In PiTrac/Hardware/Dual Pi5 Connector Board/Fabrication Files/ there are the gerber file + a zipped copy.
These should be all you need to order a board from PCBway.
All options can be left default **EXCEPT Surface Finish** it is recommended to select "HASL Lead-Free" instead of "HASL with Lead"

## Connector Board BOM

**There is always the possibility that the specific part number I have chosen will go out of stock, I have attempted to provide the "Important Parameters" necessary for selecting an alternate where it is reasonable.**
**At the time of writing this BOM is approximately $23 on Digikey**

| Quantity | Reference Designators | Manufacturer | Manufacturer Part Number | Important Parameters | Link |
|----------|-----------------------|--------------|--------------------------|----------------------|------|
| 14 | C1,C2,C3,C4,C5,C6,C9,C10,C11,C12,C14,C25,C26,C27 | Murata | GCJ188R71H104KA12D | 0603, 0.1uF, X7R, 16V+ | https://www.digikey.com/en/products/detail/murata-electronics/GCJ188R71H104KA12D/2783803
| 2 | C13,C22 | Murata | GCM1885C1H471JA16J | 0603, 470pF, C0G, 16V+ | https://www.digikey.com/en/products/detail/murata-electronics/GCM1885C1H471JA16J/2591573
| 1 | C17 | Chemi-Con | HHSE630ELL101MJC5S | 10mm case 5mm pitch, 4000Hours+@125C, 63V+, 3A+ Ripple Current @ HF | https://www.digikey.com/en/products/detail/chemi-con/HHSE630ELL101MJC5S/10486
| 1 | C18 | Murata | GRM21BR71C475KE51L | 0805, 4.7uF, X7R, 16V+ | https://www.digikey.com/en/products/detail/murata-electronics/GRM21BR71C475KE51L/6606095
| 1 | C19 | Murata | GRM31CZ72A475KE11L | 1206, 4.7uF, X7R 100V+| https://www.digikey.com/en/products/detail/murata-electronics/GRM31CZ72A475KE11L/16033916
| 1 | C20 | Murata | GCM1885C2A330JA16D | 0603, 33pF, C0G, 16V+ | https://www.digikey.com/en/products/detail/murata-electronics/GCM1885C2A330JA16D/1765191
| 1 | C21 | Murata | GCM1885C1H101JA16D | 0603, 100pF, C0G, 16V+ | https://www.digikey.com/en/products/detail/murata-electronics/GCM1885C1H101JA16D/1641641
| 1 | C23 | Murata | GRM188R72A103KA01D | 0603, 10nF, X7R, 16V+ | https://www.digikey.com/en/products/detail/murata-electronics/GRM188R72A103KA01D/2612560
| 3 | C7,C15,C16 | Murata | GCM188R71C105KA64J | 0603, 1uF, X7R, 16V+ | https://www.digikey.com/en/products/detail/murata-electronics/GCM188R71C105KA64J/4903955
| 1 | D1 | Diodes | SD103AW-7-F | SOD-123, Schottky, 10V+, 50mA+ | https://www.digikey.com/en/products/detail/diodes-incorporated/SD103AW-7-F/1306103
| 1 | D2 | Diodes | B260AE-13 | DO-214AC, Schottky, 50V+, 1A+ | https://www.digikey.com/en/products/detail/diodes-incorporated/B260AE-13/7352828
| 2 | J1,J2 | Amphenol | YK3210203000G | Alternates not recommended | https://www.digikey.com/en/products/detail/amphenol-anytek/YK3210203000G/4961227
| 2 | J3,J4 | GCT | USB4085-GF-A | Alternates not recommended | https://www.digikey.com/en/products/detail/gct/USB4085-GF-A/9859662
| 1 | J6 | Adam Tech | USB-A-S-RA | Alternates not recommended | https://www.digikey.com/en/products/detail/adam-tech/USB-A-S-RA/9832308
| 1 | J7 | Adam Tech | PH1-03-UA | Alternates not recommended | https://www.digikey.com/en/products/detail/adam-tech/PH1-03-UA/9830289
| 1 | L1 | Bourns | RLB0914-330KL | Alternates not recommended | https://www.digikey.com/en/products/detail/bourns-inc/RLB0914-330KL/2561360
| 2 | Q1,Q3 | Diodes | DMN6140L-13 | SOT-23-3, 10V+, 50mA+ | https://www.digikey.com/en/products/detail/diodes-incorporated/DMN6140L-13/4794893
| 2 | Q2,Q4 | Diodes | DMT616MLSS-13 | Alternates not recommended | https://www.digikey.com/en/products/detail/diodes-incorporated/DMT616MLSS-13/10295374
| 1 | Q5 | Diodes | MMBT3904-7-F | SOT-23-3, 10V+, 50mA+ | https://www.digikey.com/en/products/detail/diodes-incorporated/MMBT3904-7-F/814494
| 2 | R1,R4 | Yageo | RC0603FR-101ML | 0603, 1M, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-101ML/13694208
| 1 | R10 | Yageo | RC0603FR-1310RL | 0603, 10, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-1310RL/13694232
| 1 | R11 | Yageo | RL0805FR-7W0R2L | 0805, 200m, 1% | https://www.digikey.com/en/products/detail/yageo/RL0805FR-7W0R2L/2827662
| 5 | R12,R19,R21,R26,Z1 | Yageo | RC0603JR-070RL | 0603, 0 | https://www.digikey.com/en/products/detail/yageo/RC0603JR-070RL/726675
| 1 | R13 | Yageo | RC0603FR-073K16L | 0603, 3.16k, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-073K16L/727124
| 1 | R14 | Yageo | RC0603FR-07249RL | 0603, 249, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-07249RL/727085
| 1 | R15 | Yageo | RC0603FR-074K99L | 0603, 4.99k, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-074K99L/727219
| 1 | R17 | Yageo | RC0603FR-1311KL | 0603, 11k, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-1311KL/14008342
| 1 | R18 | Yageo | RC0603FR-0724K9L | 0603, 24.9k, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-0724K9L/727080
| 4 | R2,R3,R16,R24 | Yageo | RC0603FR-0710KL | 0603, 10k, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-0710KL/726880
| 1 | R22 | Yageo | RC0603FR-0713K7L | 0603, 13.7k, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-0713K7L/726933
| 2 | R23,R25 | Yageo | RC0603FR-0727RL | 0603, 27, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-0727RL/727099
| 1 | R5 | Yageo | RL1206FR-7W0R033L | 1206, 33m, 1% | https://www.digikey.com/en/products/detail/yageo/RL1206FR-7W0R033L/3886160
| 1 | R6 | Yageo | RC0603FR-1024KL | 0603, 24k, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-1024KL/14286385
| 1 | R7 | Yageo | RC0603FR-10750RL | 0603, 750, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-10750RL/14008201
| 1 | R8 | Yageo | RC0603FR-1349R9L | 0603, 49.9, 1% | https://www.digikey.com/en/products/detail/yageo/RC0603FR-1349R9L/13694149
| 1 | R9 | Yageo | RC0603JR-072RL | 0603, 2, 5% | https://www.digikey.com/en/products/detail/yageo/RC0603JR-072RL/5918445
| 2 | RV1,RV2 | Bourns | 3362W-1-501LF | Alternates not recommended | https://www.digikey.com/en/products/detail/bourns-inc/3362W-1-501LF/1088456
| 1 | U1 | Texas Instruments | UCC2813DTR-3 | Can't be substituted | https://www.digikey.com/en/products/detail/texas-instruments/UCC2813DTR-3/1911589
| 1 | U10 | Texas Instruments | SN74LVC1T45DBVR | Alternates not recommended | https://www.digikey.com/en/products/detail/texas-instruments/SN74LVC1T45DBVR/639455
| 1 | U2 | Texas Instruments | REF3025AIDBZR | Alternates not recommended | https://www.digikey.com/en/products/detail/texas-instruments/REF3025AIDBZR/1573911
| 1 | U3 | Texas Instruments | OPA357AIDBVR | Alternates not recommended | https://www.digikey.com/en/products/detail/texas-instruments/OPA357AIDBVR/1572552
| 1 | U4 | Texas Instruments | TS5A3157DBVR | Alternates not recommended | https://www.digikey.com/en/products/detail/texas-instruments/TS5A3157DBVR/705351
| 2 | U6,U8 | Texas Instruments | TLC555IDR | Alternates not recommended | https://www.digikey.com/en/products/detail/texas-instruments/TLC555IDR/276980
| 1 | U7 | Texas Instruments | SN74LVC1G08DBVR | Alternates not recommended | https://www.digikey.com/en/products/detail/texas-instruments/SN74LVC1G08DBVR/385718
| 1 | U9 | Texas Instruments | SN74LVC1G17DBVR | Alternates not recommended | https://www.digikey.com/en/products/detail/texas-instruments/SN74LVC1G17DBVR/389051

## Hardware - Bolts and Nuts

### Version 1 Base Enclosure Hardware

| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 4 | M4 × 12 mm screws | LED power supply hold-downs |
| 4 | M2.5 × 12 mm bolts + nuts | Pi board bolt-down |
| 4 | M2.5 × 10 mm bolts (3) + M2.5 × 12 mm (1) | Pi board bolt-down |
| 8 | M2.5 × 16 mm bolts + nuts | Pi camera attachment bolts |
| 6 | M4 × 12 mm bolts + nuts | Pi camera gimbal attachment |
| 2 | M5 × 12 mm bolts + nuts | Pi camera swivel mount |
| 6 | M3 × 16 mm bolts + nuts | Horizontal center-side body attachment |
| 18 | M3 × 10 mm self-tapping screws | Floor hold-down screws |
| 8 | M3 × 8 mm screws | LED and lens hold-down screws |


### Version 2 Enclosure Hardware (Work in Progress)

| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 2 | M3 × 8 mm self-tapping screws | AC power inlet plug |
| 2 | M3 × 8 mm self-tapping screws | Base box end-cap |
| 12 | M2 × 6 mm self-tapping screws | Tower back/front plate alignment |
| 4 | M4 × 12 mm self-tapping screws | Tower feet to base box |
| 6–12 | M2.5 × 8 mm self-tapping screws | Compute board to backplane |

**Hardware Note:** Stainless steel screws are stronger than black carbon steel and recommended, especially with PLA material. See [stainless steel assortment kit](https://www.googleadservices.com/pagead/aclk?sa=L&ai=DChcSEwiLuLi4w9eJAxW8Ka0GHe7XF-QYABALGgJwdg) for bulk purchasing.