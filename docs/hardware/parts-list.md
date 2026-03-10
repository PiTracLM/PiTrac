---
title: Parts List
layout: default
nav_order: 1
parent: Hardware
description: Complete parts list and shopping guide for building your PiTrac DIY golf launch monitor, including Raspberry Pi, cameras, electronics, and 3D printing materials.
keywords: PiTrac parts list, DIY golf monitor components, raspberry pi 5, global shutter camera, 3D printing materials, electronics shopping
last_modified_date: 2026-02-20
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
| 1 | USB COB LED Strip Lights – 6.56 ft | For lighting the teed-up ball. **Must produce no infrared light** | https://www.amazon.com/Aclorol-Powered-Daylight-Flexible-Backlight/dp/B0D1FYV3LM/

## Power Components

| Quantity | Hardware | Purpose | Link |
|----------|----------|---------|------|
| 1 | Meanwell LRS-75-5 | Provides plenty of power and forms base of unit | https://www.digikey.com/en/products/detail/mean-well-usa-inc/LRS-75-5/7705056
| 1 | AC Power Inlet C14 with Fuse | For base box power input | https://www.amazon.com/IEC320-Socket-Holder-Module-Connector/dp/B081ZFHRGW/
| 1 | 18ga Solid-core Wire Spools | For Input Power | https://www.amazon.com/TUOFENG-Wire-Solid-different-colored-spools/dp/B085QD9DWP
| 1 | 22ga Solid-core Wire Spools | For IR LED Wiring | https://www.amazon.com/TUOFENG-Hookup-Wires-6-Different-Colored/dp/B07TX6BX47

## V3 Connector Board BOM

**TP2 is optional**

| Quantity | Reference Designators | Manufacturer | Manufacturer Part Number | Link |
|----------|-----------------------|--------------|--------------------------|------|
| 1 | C1 | KEMET | A750BK107M1AAAE024 | https://www.digikey.com/en/products/detail/kemet/A750BK107M1AAAE024/13420054
| 2 | C2,C11 | Vishay Beyschlag/Draloric/BC Components | K105K20X7RF5TH5 | https://www.digikey.com/en/products/detail/vishay-beyschlag-draloric-bc-components/K105K20X7RF5TH5/2820552
| 6 | C3,C8,C10,C12,C14,C15 | Vishay Beyschlag/Draloric/BC Components | K104K10X7RF5UH5 | https://www.digikey.com/en/products/detail/vishay-beyschlag-draloric-bc-components/K104K10X7RF5UH5/2356754
| 1 | C4 | Vishay Beyschlag/Draloric/BC Components | K471J15C0GF5UH5 | https://www.digikey.com/en/products/detail/vishay-beyschlag-draloric-bc-components/K471J15C0GF5UH5/2823074
| 1 | C5 | Murata | RDER72A105K2M1H03A | https://www.digikey.com/en/products/detail/murata-electronics/RDER72A105K2M1H03A/4771353
| 1 | C6 | KEMET | A750MW337M1HAAE020 | https://www.digikey.com/en/products/detail/kemet/A750MW337M1HAAE020/13420041
| 1 | C7 | Vishay Beyschlag/Draloric/BC Components | K104K20X7RH5UH5 | https://www.digikey.com/en/products/detail/vishay-beyschlag-draloric-bc-components/K104K20X7RH5UH5/2356756
| 1 | C9 | Murata | RCER71H475K3K1H03B | https://www.digikey.com/en/products/detail/murata-electronics/RCER71H475K3K1H03B/4277828
| 3 | D1 | Vishay General Semiconductor - Diodes Division | SB150-E3/73 | https://www.digikey.com/en/products/detail/vishay-general-semiconductor-diodes-division/SB150-E3-73/2142188
| 1 | D2 | Lite-On Inc. | LTL-307G | https://www.digikey.com/en/products/detail/liteon/LTL-307G/669998
| 2 | D3,D4 | Vishay General Semiconductor - Diodes Division | SD103A-TR | https://www.digikey.com/en/products/detail/vishay-general-semiconductor-diodes-division/SD103A-TR/3104157
| 1 | D5 | Lite-On Inc. | LTL-307E | https://www.digikey.com/en/products/detail/liteon/LTL-307E/669997
| 2 | J1,J4 | Amphenol Anytek | YK3210203000G | https://www.digikey.com/en/products/detail/amphenol-anytek/YK3210203000G/4961227
| 2 | J2,J5 | GCT | USB4085-GF-A | https://www.digikey.com/en/products/detail/gct/USB4085-GF-A/9859662
| 1 | J3 | Adam Tech | PH1-08-UA | https://www.digikey.com/en/products/detail/adam-tech/PH1-08-UA/9830442
| 1 | J6 |  GCT | USB1125-GF-B | https://www.digikey.com/en/products/detail/gct/USB1125-GF-B/12819955
| 1 | L1 | Bourns Inc. | RLB0914-330KL | https://www.digikey.com/en/products/detail/bourns-inc/RLB0914-330KL/2561360
| 2 | Q1,Q2 | Infineon Technologies | IRLU024NPBF | https://www.digikey.com/en/products/detail/infineon-technologies/IRLU024NPBF/812400
| 1 | Q3 | Microchip Technology | TN2106N3-G | https://www.digikey.com/en/products/detail/microchip-technology/TN2106N3-G/4902377
| 1 | Q4 | onsemi | 2N3904TFR | https://www.digikey.com/en/products/detail/onsemi/2N3904TFR/458818
| 1 | R1 | Yageo | PNP300JR-73-0R15 | https://www.digikey.com/en/products/detail/yageo/PNP300JR-73-0R15/2058854
| 1 | R2 | Yageo | KNP1WSJT-52-180R | https://www.digikey.com/en/products/detail/yageo/KNP1WSJT-52-180R/9119594
| 1 | R3 | Yageo | MFR-25FRF52-60K4 | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-60K4/15056
| 1 | R4 | Yageo | MFR-25FRF52-2K2 | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-2K2/9138953
| 1 | R5 | Yageo | MFR-25FTE52-2K49 | https://www.digikey.com/en/products/detail/yageo/MFR-25FTE52-2K49/9140029
| 1 | R9 | Yageo | MFR-25FTE52-120R | https://www.digikey.com/en/products/detail/yageo/MFR-25FTE52-120R/9139747
| 1 | R7 | Yageo | MFR-25FRF52-75K | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-75K/15065
| 2 | R8,R22 | Yageo | MFR-25FRF52-1K | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-1K/14891
| 1 | R10 | Yageo | KNP100JR-73-0R1 | https://www.digikey.com/en/products/detail/yageo/KNP100JR-73-0R1/9119173
| 1 | R11 | Yageo | MFR-25FRF52-604R | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-604R/14870
| 1 | R12 | Yageo | MFR-25FBF52-6K04 | https://www.digikey.com/en/products/detail/yageo/MFR-25FBF52-6K04/13177
| 2 | R13,R21 | Yageo | MFR-25FRF52-2K | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-2K/14920
| 1 | R14 | Yageo | MFR-25FRF52-3K | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-3K/9139012
| 1 | R15 | Yageo | MFR-25FRF52-10K | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-10K/14626
| 4 | R16,R19,R20,R23 | Yageo | ZOR-25-R-52-0R | https://www.digikey.com/en/products/detail/yageo/ZOR-25-R-52-0R/18795
| 1 | R17 | Yageo | MFR-25FRF52-200R | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-200R/14824
| 1 | R18 | Yageo | MFR-25FRF52-10K | https://www.digikey.com/en/products/detail/yageo/MFR-25FRF52-10K/14626
| 1 | TP2 | Keystone Electronics | 5011 | https://www.digikey.com/en/products/detail/keystone-electronics/5011/255333
| 1 | U1 | Texas Instruments | MC33063AP | https://www.digikey.com/en/products/detail/texas-instruments/MC33063AP/717430
| 1 | U2 | onsemi | LM317LBZRAG | https://www.digikey.com/en/products/detail/onsemi/LM317LBZRAG/1476805
| 1 | U3 | Microchip Technology | MCP1407-E/P | https://www.digikey.com/en/products/detail/microchip-technology/MCP1407-E-P/1228640
| 1 | U5 | Microchip Technology | MCP4801-E/P | https://www.digikey.com/en/products/detail/microchip-technology/MCP4801-E-P/2332804
| 1 | U6 | Microchip Technology | MCP3202-CI/P | https://www.digikey.com/en/products/detail/microchip-technology/MCP3202-CI-P/305924

## IRLED BOM

| Quantity | Reference Designators | Manufacturer | Manufacturer Part Number | Link |
|----------|-----------------------|--------------|--------------------------|------|
| 10 | D6,D7,D8,D9,D10,D11,D12,D13,D14,D15 | Vishay Semiconductor Opto Division | VSMA1085400 | https://www.digikey.com/en/products/detail/vishay-semiconductor-opto-division/VSMA1085400/15786357


## Hardware - Bolts and Nuts

### Version 2 Enclosure Hardware (Work in Progress)

| Quantity | Hardware | Purpose |
|----------|----------|---------|
| 2 | M3 × 8 mm self-tapping screws | AC power inlet plug |
| 2 | M3 × 8 mm self-tapping screws | Base box end-cap |
| 12 | M2 × 6 mm self-tapping screws | Tower back/front plate alignment |
| 4 | M4 × 12 mm self-tapping screws | Tower feet to base box |
| 4 | M2.5 × 8 mm self-tapping screws | To mount Pi to backplane. OPTIONALLY - if using threaded inserts, use M2.5x6 allan-head bolts instead. |
| 4 | M3 × 8 mm self-tapping screws | Version 2 Connector board to backplane. OPTIONALLY - if using threaded inserts, use M3x6 allan-head bolts instead. |
| 4 | M2.5 x 4 x 3.5 mm threaded inserts | Optional if you want to use the (classier) inserts and bolts to mount the Pi to backplane. 4 inserts per Pi, so typically 4 |
| 4 | M3 x 4 x 5 mm threaded inserts | Optional if you want to use the (classier) inserts and bolts to mount the V2 Connector Board to the backplane.  |


### Version 1 Base Enclosure Hardware (DEPRECATED)

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



**Hardware Note:** Stainless steel screws are stronger than black carbon steel and recommended, especially with PLA material. See [stainless steel assortment kit](https://www.googleadservices.com/pagead/aclk?sa=L&ai=DChcSEwiLuLi4w9eJAxW8Ka0GHe7XF-QYABALGgJwdg) for bulk purchasing.  See also, e.g., [Ktehloy 400PCS Metric Threaded Inserts Kit X00401F0FF] (https://www.amazon.com/Ktehloy-Threaded-Assortment-Printing-Components/dp/B0CLKDPN65/ref=sr_1_3?crid=19PRGAMM0LHSC&dib=eyJ2IjoiMSJ9.Rwsrmvqhye5n_e2-oLfoTLv-TOZLNmyo9SCxwiWbrBF3F48asfcsFaweejcdlejptJv2IgbgSI9b_fYNDvP0z63_HYLbkK1DxQJRovFyJOPbu7kot4lM8tWm0fSAduOkOMHFGON2AOGW_PzK8Y1bGGbTOYQRmvlBeWGJrrqeBAx06ICeqf45Rs377sifWzJxeMTir0taClnzKET0RhBHmPASUQJtJrpnMhXlvtMGOcY.tjo7j88V1UyOlHfVfn5sdCBAxN53_RYHB8SW6v9ZP50&dib_tag=se&keywords=Ktehloy+%22400PCS%22+Metric+Threaded+Inserts+Kit&qid=1761691920&sprefix=ktehloy+400pcs+metric+threaded+inserts+kit%2Caps%2C126&sr=8-3) 
