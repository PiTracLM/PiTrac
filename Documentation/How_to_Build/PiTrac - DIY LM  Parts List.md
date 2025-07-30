**PiTrac \- DIY LM  Parts Lists**

**Computing Hardware:**

| Quantity | Hardware | Purpose |
| :---- | :---- | :---- |
| 1 | [Raspberry Pi 5](https://www.raspberrypi.com/products/raspberry-pi-5/) and power supply, 4 GB minimum, 8 FB recommended. | Getting a bundled kit with the Pi and power supply and some other things can be economical if you don’t have any Pi-related stuff yet. |
| 1 | [MAY BE DEPRCATED SOON] Raspberry Pi 4 Model B](https://www.raspberrypi.com/products/raspberry-pi-4-model-b/) and power supply, 4 GB minimum.  It may also make sense for a little more money just to get a second Pi 5\.  PLEASE NOTE - An experimental version of a PiTrac that requires only a single Pi 5 appears to be working and the code has been checked in.  So, if you're building a new PiTrac, you probably don't need a second Pi. |  |
| 1 | [Pimoroni NVMe Base for Raspberry Pi 5 \- PIM699](https://www.adafruit.com/product/5845) (optional, but recommended for speed and longevity).   [This board](https://www.amazon.com/gp/product/B0CQ4D2C9S/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1) or the Geekworm M901 PCIe to M.2 HAT Key-M NVMe SSD PIP PCIe Peripheral Board for Raspberry Pi 5 Support M.2 2230/2242/2260/2280 SSD works well, too.  Whatever you use, try to get one that leaves access for jumpers into the GPIO pins. | For the Pi 5, this allows use of an NVMe memory drive instead of an MicroSD card. |
| 1 | An [NVMe solid state (SSD) memory drive](https://www.amazon.com/gp/product/B0BGFRZDTB/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1) (optional, but recommended for speed and longevity) | Typically with an M.2 M key edge connector, in the 2230 and 2242 form factors.  Should probably get at least 256 GB capacity. |
| 2 | Micro SD cards (to bootstrap the Pi’s). | We recommend 64GB size |
| 1 | An Active Cooler fan for the Pi 5 (optional) | The Pi 5 can get pretty hot if you’re doing a lot of big compiles on it.  If so, having a cooler seems like decent insurance. |

**Hardware \- Bolts and Nuts:[^1]**

| Quantity | Hardware | Purpose |
| :---- | :---- | :---- |
| 4 | M4 x 12 screws | LED Power supply hold-downs |
|  |  |  |
| 4 | 2.5M x 12mm bolt | Pi 1 Board Bolt-down |
| 4 | 2.5 Hex Nuts | Pi 1 Board Bolt-down |
| 3 | 2.5M x 10mm bolt | Pi 2 Board Bolt-down |
| 1 | 2.5M x 12mm bolt (odd one\!) | Pi 2 Board Bolt-down |
|  |  |  |
| 2 x 4 | 2.5M x 16mm 2.5 Hex Nuts | Pi Camera Attachment Bolts (to back plate) |
| 3 x 2 3 x 2 | M4 x 12mm  M4 Hex Nuts  | Pi Cam Gimbal Attachment Hinge (2 cameras and strobe gimbal) |
| 2 x 1 2 x 1 | M5 x 12 M5 Hex Nut | Pi Cam (Pan) Swivel Mount (on bottom of mount) |
|  |  |  |
| 3 x 2 | M3 | Horizontal Center-Side Body Attachment |
| 4 | 2.5 x 10 or 12 | Connector Board Bolt-down |
| 3 x 6 \= 18 | M3 x 10mm | Floor hold-down screws |
|  |  |  |
| 2 x 4 | 2.5M x 12mm bolt | Connector Board Mounting Bolts |
| 2 x 4 | 2.5 Hex Nuts | Connector Board Mounting Bolts |
| 8 | M3 x 8mm | LED and LED Lens Hold-down screws |
|  |  |  |
| 2 2 | M3 x 12mm M3 Nuts | Half-Join bolts and nuts for ceiling level walls |
| 2 2 | M3 x 16mm M3 Nuts | Half-Join bolts and nuts for base and middle level walls |
| 5 x 4 | M3 x 10mm self-tapping screws | These screws secure the five “floor” components to their respective “wall” components and/or other floor components |

**Camera and Lighting Hardware:**

| 2  | [Raspberry Pi Global Shutter Camera – CS Lens Mount](https://www.adafruit.com/product/5702)&nbsp; Alternatively, the following camera also appears to work with PiTrac.  This camera should be much easier to work with than the 'official' Raspberry Pi camera, but as of July 16, 2025, we are still waiting on a fix that prevcents using two such cameras on the same Pi 5.  Still, hopefully that will be fixed soon, so consider trying it out!  [innomaker GS Camera Module with IMX296 Mono Sensor ](https://www.amazon.com/gp/product/B093BY2TK2/ref=ewc_pr_img_1?th=1)| The cameras currently used by the LM.  The Pi cameras do not come with lenses (see below), but the Innomakers come with M12 lenses.  The included M12 3.6mm lenses are not tested out with PiTrac yet, but may work if the ball is close.  The Innomakers also come with a short ribbon cable to connect the Pi 5 to the camera, but it's probably only long enough to work with one of the two PiTrac cameras, so you'll want a longer one. |
| :---- | :---- | :---- |
| 1 | [Raspberry Pi 5 FPC Camera Cable \- 22-pin 0.5mm to 15-pin 1mm \- 300mm long](https://www.adafruit.com/product/5819) | The current Pi Cameras have a CSI cable that needs a conversion to fit into the smaller CSI ports on the Pi 5\.  The 200mm should probably work as well. |
|  | [200 mm Flex Cable for Pi 4](https://www.adafruit.com/product/2087) | This likely comes with most Pi Cameras, so may not be necessary |
| 2 | [6mm 3MP Wide Angle Lens for Raspberry Pi HQ Camera \- 3MP](https://www.adafruit.com/product/4563) | Wide angle lens for the Pi GS cameras (which come without a lens) |
| 1 | [1" x 1", Optical Cast Plastic IR Longpass Filter](https://www.edmundoptics.com/p/1quot-x-1quot-optical-cast-plastic-ir-longpass-filter/5421/) (1.5mm nominal thickness) | Blocks visible light below ~700nm from entering the Camera 2 sensor |
|  |  |  |
| 1 | [60 / 120 Degree LED Lens Optical Glass 44mm \+ Reflector Collimator \+ Fixed Bracket for 20W 30W 50W 100W COB High Power Chip (60Degree 100W len kit)](https://www.amazon.com/dp/B09XK7QTV5?ref=ppx_yo2ov_dt_b_fed_asin_title) \Consider getting both a 60 an 120-degree lens for flexibility. | The lens helps focus the infrared IR light. |
| 1 | [High Power LED Chip IR 730nm (you want the 730nm) 850nm 940nm 3W 5W 10W 20W 30W 50W 100W COB Integrated Matrix Light Beads for Night Vision Camera (Select the 730NM, 100W 1Pieces)](https://www.amazon.com/dp/B09DNRT2R4?ref=ppx_yo2ov_dt_b_product_details&th=1)  | The 730nm IR Led used for strobe light.  NOTE \- this device can get very hot if left constantly on for even a few seconds.  PiTrac just uses it in short pulses. |
| 1 | [Aclorol USB COB LED Strip Lights for TV 5V USB Powered LED Tape Lights Daylight White 6.56FT 320Leds/M 6000K COB Flexible Light Strip TV Led Backlight for Bedroom Under Cabinet Kitchen DIY](https://www.amazon.com/Aclorol-Powered-Daylight-Flexible-Backlight/dp/B0D1FYV3LM/ref=asc_df_B0D1FYV3LM?mcid=70db06b6e26231f3961ef52c9615b991&hvocijid=2003913134214569652-B0D1FYV3LM-&hvexpln=73&tag=hyprod-20&linkCode=df0&hvadid=721245378154&hvpos=&hvnetw=g&hvrand=2003913134214569652&hvpone=&hvptwo=&hvqmt=&hvdev=c&hvdvcmdl=&hvlocint=&hvlocphy=9028778&hvtargid=pla-2281435177378&psc=1) | LED strip for lighting the teed-up ball- long enough to double it for more light. 
NOTE - The Strip should produce NO infrared light, or else it will interfere with the IR strobing that the system uses.
|

**Power-Related Parts:**

| 1 | RECOMMENDED NOW:   At this point in time, it's probably safe (and better) to get a higher-power LED driver that can over-voltage the LED.  For example, [a 3000mA 18-39V 120W driver](https://www.aliexpress.com/i/2251832563139779.html?gatewayAdapt=4itemAdapt) | Driver for LED strobe array. Because of the short pulses that are used in the LM, a much smaller (and cheaper) driver may also work, possibly even a non-constant-current driver.   ORIGINAL VERSION:  LED Driver, 12-26V, Output Current DC 3600mA, such as: https://www.aliexpress.us/item/2251832563139779.html NOTE - Also need a power-plug for some of these drivers that just come with power wires.  For the US, see, e.g., [this](https://www.amazon.com/sspa/click?ie=UTF8&spc=MTozNDg3Njk4NDA4NzA1MDMwOjE3MzY4NjY2NTk6c3BfZGV0YWlsX3RoZW1hdGljOjMwMDE1OTE3MDMwOTgwMjo6Ojo&url=%2Fdp%2FB0CX1N6QH9%2Fref%3Dsspa_dk_detail_2%3Fpsc%3D1%26pd_rd_i%3DB0CX1N6QH9%26pd_rd_w%3DGQ1BX%26content-id%3Damzn1.sym.f2f1cf8f-cab4-44dc-82ba-0ca811fb90cc%26pf_rd_p%3Df2f1cf8f-cab4-44dc-82ba-0ca811fb90cc%26pf_rd_r%3DQR7BKYRYDGSHKFAMD4S7%26pd_rd_wg%3DJjPV7%26pd_rd_r%3Dacea77bd-162b-41bb-9994-5c2cf82355d8%26s%3Dhi%26sp_csd%3Dd2lkZ2V0TmFtZT1zcF9kZXRhaWxfdGhlbWF0aWM)  |
| :---- | :---- | :---- |
| 1 | A 5V USB-B power supply with a male micro USB connector  | This is currently necessary to run the Connectro Board.  The supply is used as an isolated, independent (downstream of a transformer) power plane to try to keep the Pi’s and the Pi Camera safe. |
|  |  |  |

**Connector Board Parts:**

| Item | Qty | Reference(s) | Value | LibPart | Footprint / Details |  |
| :---- | :---- | :---- | :---- | :---- | :---- | ----- |
| 1 | 1 | J3 | \~ | [USB B Micro Female Pinboard](https://www.amazon.com/Pinboard-MELIFE-Interface-Adapter-Breakout/dp/B07W6T97HZ/ref=sr_1_1?crid=1KITM9DEDEQMA&dib=eyJ2IjoiMSJ9.fGuXBUJw1cZvJFMJklJhkSh4wNEQKdwHnb94uj1k7QTRfzEl71vaSvgkvNQ0aqipm8Ua3bK7zTYznxLIBgNAD4XJGFw08Mivt69vXxTpzaH_qrzlbYL8n7s_uwWCz7OMd4BUwKb8m4OW2InoWsrjHJH9RNJImM2AJTL0pbrro1OPdtfIv6AMsSSx1s9T9YNZyzrvz_o5B1rXAK7BsCBoX6edEQggz2toWMxzcdXa3PJ-cSoZUAo8mb6lIgYGhKnPElFTuXkOqoh-kAGtKJSqudp3cwFBwl2wzeDo9ioWl7U.VxdrpEbIBjzibUhD8wjWdpWVrk3AT8vYemC8ZQVRUQA&dib_tag=se&keywords=10pcs+Female+Micro+USB+to+DIP+5-Pin+Pinboard%2C+2.54mm+Micro+USB+Type+Interface+Power+Adapter+Board+5V+Breakout+Module&qid=1732903628&s=electronics&sprefix=10pcs+female+micro+usb+to+dip+5-pin+pinboard%2C+2.54mm+micro+usb+type+interface+power+adapter+board+5v+breakout+module%2Celectronics%2C173&sr=1-1) | The included Pin headers can be used for the parts below |  |
| 2 | 2 | R1, R4 | 330 ohm R | Resistor A decent assortment kit is [here](https://www.amazon.com/gp/product/B003UC4FSS/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1) | Resistor\_THT:R\_Axial\_DIN0207\_L6.3mm\_D2.5mm\_P10.16mm\_Horizontal |  |
| 3 | 2 | R2, R3 | 270 ohm R | Resistor | Resistor\_THT:R\_Axial\_DIN0207\_L6.3mm\_D2.5mm\_P10.16mm\_Horizontal |  |
| 5 | 1 | Sys1\_Conn1 | \~ | 4 Pin Header | Connector\_PinHeader\_2.54mm:PinHeader\_1x04\_P2.54mm\_Vertical |  |
| 6 | 1 | Sys2\_Conn1 | \~ | 3 Pin Header | Connector\_PinHeader\_2.54mm:PinHeader\_1x03\_P2.54mm\_Vertical |  |
| 7 | 2 | U2, U3 | H11L1 | [Isolator:H11L1]([https://www.amazon.com/dp/B09KM2BJCF?ref_=pe_386300_442618370_TE_sc_as_ri_0](https://www.amazon.com/10PCS-H11L1M-Photoelectric-Coupler-Optocoupler/dp/B09PK3V339)) | Package\_DIP:DIP-6\_W7.62mm. NOTE: Make sure you get the DIP pin style, NOT the shorter surface-mount style.  The Connector Board is setup for through-hole pins, not surface mounting. |  |
| 8 | 1 | U4 | 74HC04 | 74xx:74HC04 | Package\_DIP:DIP-14\_W7.62mm\_Socket |  |
| 9 | 1 | U5 | \~ | [Dual\_Mos\_Driver\_Module](https://www.amazon.com/Anmbest-High-Power-Adjustment-Electronic-Brightness/dp/B07NWD8W26/ref=sims_dp_d_dex_ai_speed_loc_mtl_v5_t1_d_sccl_2_1/132-9837543-1351009?pd_rd_w=xeol9&content-id=amzn1.sym.281550a9-05fa-4fa0-a033-b1923adca8ef&pf_rd_p=281550a9-05fa-4fa0-a033-b1923adca8ef&pf_rd_r=WXGFBGSTH336WSAYW2XQ&pd_rd_wg=SokZb&pd_rd_r=2d219084-db5a-41cc-b5d7-b01ed55844ec&pd_rd_i=B07NWD8W26&th=1) | Verdant\_Custom\_Footprint\_Library:PCB Dual MOS Driver Module |  |
|  | 1 | 2.5M Bolt 2.5M Nut |  |  | These help mechanically tie down the USB-B power connector to the board |  |
|  |  | \<Optional\> |  |  |  |  |
|  | 2 | 6 \- pin DIP socket |  | A decent assortment kit is [here](https://www.amazon.com/dp/B01GOLSUAU?ref=ppx_yo2ov_dt_b_fed_asin_title) | The opto-isolators and the hex inverter chip are probably the most likely to be blown up or injured by solder over-temps.  So best to put them in sockets |  |
|  | 1 | 14 \- pin DIP socket |  | A decent assortment kit is [here](https://www.amazon.com/dp/B01GOLSUAU?ref=ppx_yo2ov_dt_b_fed_asin_title) |  |  |

**Miscellaneous Parts/Supplies:**

| Quantity | Hardware | Purpose / Notes |
| :---- | :---- | :---- |
| 2 |  [Ideal Industries 30-102 Power Plug](https://www.amazon.com/Ideal-Industries-30-102-Power-72427/dp/B01LYF1WV9/ref=sr_1_10?crid=35QTOVMFFT8MM&dib=eyJ2IjoiMSJ9.E2lbqLFKVG_hmleCqoMKymmILpx7xHQqbjKLlS_O8biic815YLA-1NACjC7tLtjM1exKso1Yz_65dH1SEbMni2IVp4idBpijPJhdQ_dqc_jCaEmkTbkPx0xoOe5u_wFYZDU-708_oXsrAmTNBXO-a2iAVH3lMZzeo9MZ6zFEO29IgxN_2m_0GrCkWTMORn0iifYao1peHnHenuPGSKCOTzvpolrumaoD-CkeZ5nvs29wAsiRDyq-Yq2K7tfmSzrQslG_ZpLzrx5-9GZQe55ktJDEK1bqKpEjHi-F_Lg1tuk.lAcQi1tp-upRzLoq8eleSXFSf1Vet_Xild25OSMBkYI&dib_tag=se&keywords=ideal+power+plug&qid=1732650183&sprefix=ideal+power+plug%2Caps%2C174&sr=8-10). | Enables easy connect/disconnects |
| N/A | 3 and 4-pin ribbon cables For example, [here](https://www.amazon.com/Kidisoii-Dupont-Connector-Pre-Crimped-5P-10CM/dp/B0CCV1HVM9/ref=sr_1_2_sspa?crid=82PRY332YY24&dib=eyJ2IjoiMSJ9.QGbaFF62mgZ1Tf0J7CajkKXRwTNzXfW_5IJDpp-KFNMSPxr3NJETCkY5IC0OIC6NydeuYNxh2IMnpu14CuIxCxLVt4T4kUKlsGMMPk0uwwu7oKUnh2qI2YGvPNmbGfKPCZE6EYSST4_MDqKRP2VMQDk5slq7dKh0rdLvqEMAjboPrVlVx0sd3ShyQsdiUN9ljqd6q-bfi_ZVY2jKoBiwVuMgkZs-BkA3jRBQvkzWpos.aLN0QbPnnKDjbELWeYKRE5SrUWDe-Ivk47o6BkYfL04&dib_tag=se&keywords=ribbon+cable+with+female+connectors&qid=1733175902&sprefix=ribbon+cable+with+femaleconnectors%2Caps%2C143&sr=8-2-spons&sp_csd=d2lkZ2V0TmFtZT1zcF9hdGY&psc=1). | These are used for the wiring harness connects between the various GPIO pins and the Connector Board |
| 1 | 15.5cm x 24cm plexiglass anti-ball-strike protective window | Protects the cameras and strobe **NOTE:**  Ensure the plexi is not IR-blocking\!  Most hardware stores can cut plexi in this size for a few US$ |
| 1 | Power Strip | A relatively short power strip that is compact enough to fit into the enclosure. 10” or less, and with some type of power filtering to protect the rest of the components |

[^1]:  Stainless Steel seems to be stronger than black carbon steel, though both can work.  Especially in the stiffer PLA material, stainless is preferable because black carbon can break if torqued too much..  For parts supply example see [HanTof 525 Pcs 304 Stainless Steel M2 M3 M4 Phillips Pan Head Self Tapping Wood screws Assortment Kit](https://www.googleadservices.com/pagead/aclk?sa=L&ai=DChcSEwiLuLi4w9eJAxW8Ka0GHe7XF-QYABALGgJwdg&co=1&ase=2&gclid=Cj0KCQiAlsy5BhDeARIsABRc6ZvLmaSF7SMXMVEWsWj3rp8S5qOvISkRMdr9czJDXK6-_GcV_X18lyoaApxdEALw_wcB&ohost=www.google.com&cid=CAESVeD2Qt_A3T4eLbWpcXoKb1T4jyPt8OTe6U2I9Uze-kjy3MAArJhoS9pYvi39aDroogjezyRvCcOJ56x4hYKkL8BaTouv69VFkW3lUm7VcRcYOp8m49s&sig=AOD64_3FizAX65usODanOujaKxqh5_gQbg&ctype=5&q=&nis=4&ved=2ahUKEwj92Kq4w9eJAxU1JzQIHakTKO8Q9aACKAB6BAgEEAw&adurl=)., 


**NOTE \-The following is a work in progress \- Version 2 of the Enclosure is not complete**

**Enclosure Ver. 2 \- Hardware \- Bolts and Nuts:[^2]**

| Quantity | Hardware | Purpose |
| :---- | :---- | :---- |
| 2 | M3 x 8 self-tapping screws | AC Power inlet plug (side of base box) (if using a PSU, or potentially putting a LED driver inside the basebox. |
| 2 | M3 x 8 self-tapping screws | Secures the end-cap of the base box into the rest of the box |
| 2 | M3 x 8 machine screws | Secures PSU to bottom of base box.  No PSU will generally be used if using the original Connector Board variant of the Version 2 enclosure. |
| 3 | M3 x 10 machine screws (or x 8\) | Secures PSU to the side of the base box (If using the new PiTrac Compute Board).   NOTE \- in order to efficiently use screw assortment sets, either x8 or x10 screws can be used.  There is space in the PSU for either, so just use what you have more of. |
| 12 | M2 x 6 self-tapping screws | Secures and aligns the tower back and front plate parts |
| 2 x 4 | M3 x 6 self-tapping screws | Secures Pi Microcontrollers to Backplane (Only if using Compute Board variant and Pi Microcontrollers instead of Compute Modules |
| 4 | M4 x 12  self-tapping screws | Secures tower (feet) to top of base box.  Could use longer screws if they don’t interfere with the power supply. |
| 2 | Pan head M4\*12 or \*16 self-tapping screws  | For the top of the Shank Shield.  Pan-head is preferred in order to avoid countersinking, but could also use flat-head. |
| 2 | Flat head self-tapping M4\*12 or \*14 screws | For the bottom of the Shank Shield \- screws go through the front of the basebox and the shield. |
| 4 | M4 x 12 | Secures Case Cover to the top of the Basebox |
| 6 12 (variant) | Self-tapping M2.5 x 8 screws | Secure Compute Board to the Backplane of the Tower.  If using the original Connector Board and two Raspberry Pi’s, will need 12, not 6 screws. |
| 5 | Heat Shrink Ring-type Wire Connectors | [Ring connectors](https://www.amazon.com/dp/B0CYM3J44Q?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1) to connect the various power supply wires to the screw terminals of the PSU. |

**Enclosure Ver. 2 \- Components (WORK IN PROGRESS)**

| 1 | AC 250V 15A IEC320 C14 Male Power Cord Inlet Socket with Fuse Holder,3Pin Inlet Module Power Connector Socket with 3pcs Wires | [AC Power inlet plug](https://www.amazon.com/IEC320-Socket-Holder-Module-Connector/dp/B081ZFHRGW/ref=sr_1_6?crid=2DKBAQMH44GRG&dib=eyJ2IjoiMSJ9.dVV6QBUldjPNvkPlVHiLfdJ7bh1G4vsgRURNHlImOsleAhVwWEofnFBs0m5Lk6ogy7vOgGHoAFFR_oLS-pEd3-yleomym5goucq5RbCF11b30qvN1eTzFk4gn1GctrCMLNubL2pRlvl83XN1o9heIluAKTbpAj2r3uixQZV2RouUYEV_Yq6w90AKK-5ooXiIXEASn65fOQqx8Xz_x2V5A1LaAUTsmcEIpKq1L1Aohp7K9hwGkhZQenB-uTFkBZapM1ve16_AImKE4T0C-G9usz1IYndjYKeKAWrRWddZb2U.B9djjQkUKJMXT4UfZy4FXMS6fc36kzNO3TUmFgmhFkU&dib_tag=se&keywords=filshu+ac+inlet+plug+c14&qid=1744312070&sprefix=filshu+ac+inlet+plug+c14%2Caps%2C122&sr=8-6) (for the side of base box)  (when using a PSU, or potentially putting a LED driver inside the basebox. |
| :---- | :---- | :---- |
| 1 | Mean Well LRS-150-24 150W Single Output Switching Power Supply(24V 6.5A) (ONLY when we get the new PiTrac Compute Board that doesn't use AC adapters) | [150 W Power supply unit.](https://www.amazon.com/dp/B07GTY6R4H?ref=ppx_yo2ov_dt_b_fed_asin_title) |
| TBD | TBD | TBD |

[^1]:  Stainless Steel seems to be stronger than black carbon steel, though both can work.  Especially in the stiffer PLA material, stainless is preferable because black carbon can break if torqued too much..  For parts supply example see [HanTof 525 Pcs 304 Stainless Steel M2 M3 M4 Phillips Pan Head Self Tapping Wood screws Assortment Kit](https://www.googleadservices.com/pagead/aclk?sa=L&ai=DChcSEwiLuLi4w9eJAxW8Ka0GHe7XF-QYABALGgJwdg&co=1&ase=2&gclid=Cj0KCQiAlsy5BhDeARIsABRc6ZvLmaSF7SMXMVEWsWj3rp8S5qOvISkRMdr9czJDXK6-_GcV_X18lyoaApxdEALw_wcB&ohost=www.google.com&cid=CAESVeD2Qt_A3T4eLbWpcXoKb1T4jyPt8OTe6U2I9Uze-kjy3MAArJhoS9pYvi39aDroogjezyRvCcOJ56x4hYKkL8BaTouv69VFkW3lUm7VcRcYOp8m49s&sig=AOD64_3FizAX65usODanOujaKxqh5_gQbg&ctype=5&q=&nis=4&ved=2ahUKEwj92Kq4w9eJAxU1JzQIHakTKO8Q9aACKAB6BAgEEAw&adurl=)., 

[^2]:  You may choose to buy some assortment sets of screws and bolts.  That hardware on this list can be satisfied by a combination of:  
