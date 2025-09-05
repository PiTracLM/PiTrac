**Description:**

A small PCB that supports:

1. high-speed (up to 1MHz), isolated switching of a high-power signal (12V, 100W) to a device such as an LED strobe; and
2. connecting a GPIO camera shutter signal from one Raspberry Pi to the external trigger (XTR) of a Pi Global Shutter (GS) Camera that is connected to a _second_ Raspberry Pi.

The board uses high-speed opto-isolators to keep the power plane of the two Pi’s isolated from each other and from the 12V switching circuit.

The PCB itself can be manufactured fairly inexpensively from the fabrication outputs at place like <https://jlcpcb.com/> and <https://pcbway.com> .

The [PiTrac DIY golf launch monitor project](https://hackaday.io/project/195042-diy-golf-launch-monitor) is using this board to drive its infrared strobe light and to allow one Pi to trigger the shutter of a second Pi. This is all used to capture the stop-motion images for the monitor.

Please consider supporting this work and the DIY LM project here: <https://ko-fi.com/PiTrac>  

**Parts Needed for Assembly:**

| **Item** | **Qty** | **Reference(s)** | **Value** | **LibPart** | **Footprint / Details** |
| --- | --- | --- | --- | --- | --- |
| 1   | 1   | J3  | ~   | [USB B Micro Female Pinboard](https://www.amazon.com/Pinboard-MELIFE-Interface-Adapter-Breakout/dp/B07W6T97HZ/ref=sr_1_1?crid=1KITM9DEDEQMA&dib=eyJ2IjoiMSJ9.fGuXBUJw1cZvJFMJklJhkSh4wNEQKdwHnb94uj1k7QTRfzEl71vaSvgkvNQ0aqipm8Ua3bK7zTYznxLIBgNAD4XJGFw08Mivt69vXxTpzaH_qrzlbYL8n7s_uwWCz7OMd4BUwKb8m4OW2InoWsrjHJH9RNJImM2AJTL0pbrro1OPdtfIv6AMsSSx1s9T9YNZyzrvz_o5B1rXAK7BsCBoX6edEQggz2toWMxzcdXa3PJ-cSoZUAo8mb6lIgYGhKnPElFTuXkOqoh-kAGtKJSqudp3cwFBwl2wzeDo9ioWl7U.VxdrpEbIBjzibUhD8wjWdpWVrk3AT8vYemC8ZQVRUQA&dib_tag=se&keywords=10pcs+Female+Micro+USB+to+DIP+5-Pin+Pinboard%2C+2.54mm+Micro+USB+Type+Interface+Power+Adapter+Board+5V+Breakout+Module&qid=1732903628&s=electronics&sprefix=10pcs+female+micro+usb+to+dip+5-pin+pinboard%2C+2.54mm+micro+usb+type+interface+power+adapter+board+5v+breakout+module%2Celectronics%2C173&sr=1-1) | The included pin headers can be used for the parts below |
| 2   | 2   | R1, R4 | 330 ohm R | Resistor (1/4 Watt)<br><br>A decent assortment kit is [here](https://www.amazon.com/gp/product/B003UC4FSS/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1) | Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal |
| 3   | 2   | R2, R3 | 270 ohm R | Resistor (1/4 Watt)| Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P10.16mm_Horizontal |
| 4   | 3   | Shutter1, Strobe1, Sys1_GND1 | ~   | &lt;These are just test-point holes to solder test wires into&gt; | TestPoint:TestPoint_THTPad_2.5x2.5mm_Drill1.2mm |
| 5   | 1   | Sys1_Conn1 | ~   | 4 Pin Header | Connector_PinHeader_2.54mm:PinHeader_1x04_P2.54mm_Vertical |
| 6   | 1   | Sys2_Conn1 | ~   | 3 Pin Header | Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical |
| 7   | 2   | U2, U3 | H11L1 | [Isolator:H11L1](https://www.amazon.com/dp/B09KM2BJCF?ref_=pe_386300_442618370_TE_sc_as_ri_0) | Package_DIP:DIP-6_W7.62mm |
| 8   | 1   | U4  | 74HC04 | 74xx:74HC04 | Package_DIP:DIP-14_W7.62mm_Socket |
| 9   | 1   | U5  | ~   | [Dual_Mos_Driver_Module](https://www.amazon.com/Anmbest-High-Power-Adjustment-Electronic-Brightness/dp/B07NWD8W26/ref=sims_dp_d_dex_ai_speed_loc_mtl_v5_t1_d_sccl_2_1/132-9837543-1351009?pd_rd_w=xeol9&content-id=amzn1.sym.281550a9-05fa-4fa0-a033-b1923adca8ef&pf_rd_p=281550a9-05fa-4fa0-a033-b1923adca8ef&pf_rd_r=WXGFBGSTH336WSAYW2XQ&pd_rd_wg=SokZb&pd_rd_r=2d219084-db5a-41cc-b5d7-b01ed55844ec&pd_rd_i=B07NWD8W26&th=1) | Verdant_Custom_Footprint_Library:PCB Dual MOS Driver Module |
|     | 2   | 2.5M Bolt<br><br>2.5M Nut |     |     | These help mechanically tie down the USB-B power connector to the board |
|     | 2   | 6 - pin DIP socket |     | &lt;Optional&gt;<br><br>A decent assortment kit is [here](https://www.amazon.com/dp/B01GOLSUAU?ref=ppx_yo2ov_dt_b_fed_asin_title) | The opto-isolators and the hex inverter chip are the most likely to be blown up or injured by solder over-temps.  So best to put them in sockets. |
|     | 1   | 14 - pin DIP socket<br><br>&lt;Optional&gt; |     | &lt;Optional&gt; |     |

**Note:** A 5V USB-B power supply with a male micro-USB connector is currently necessary to run the board. The supply is used as an isolated, independent (downstream of a transformer) power plane to try to keep the Pi’s and the Pi Camera safe.

**Final view of top/bottom of completed PCB:**

![image](https://github.com/user-attachments/assets/7a54ad2d-1baa-4b5b-b4bf-d26b5a67e94e)

![image](https://github.com/user-attachments/assets/fc84db4c-f43e-41be-bc8f-8edf2c7179b1)


**NOTE:** Don’t be like the people who made the above boards. For example, use flux-cleaner to get rid of the excess flux when you are done!

**Assembly Notes (see supporting images of the board):**

1. If desired (we suggest it), solder the two 6-Pin DIP sockets and one 14 pin DIP socket for the H11 opto-isolators and the 740s4 hex inverter.
2. Use the two 2.5M bolts and nuts (nuts on the bottom of the PCB) to secure the USB board to the lower-left corner. Strip 1” of a thin wire bare and cut in two half-inch pieces that will fit through the VCC and GND holes of the USB board and through the back of the Connector Board PCB. Bend the ends to hold the wires in place and solder and trim.
3. Solder a header pin post (see picture) into the GND and TRIG holes on the left side of the Dual MOS Driver Module so that at least 2mm of pin sticks out the bottom. Using these headers makes it easier to test-probe the trigger signal. Then insert the lower parts of the pins (as well as the four parts of the pins sticking out below the screw terminals) into the Connector Board PCB and solder. Alternatively, the same wire-through-the component-and-board technique used for the USB board can be used here. The four terminals below the screw terminals are not electrically important for the PCB, but soldering them helps mechanically secure the Dual-MOS switching component.
4. It is useful (but not necessary) to solder three wires to the test ports on the left of the PCB. When something is not working, the ability to hook an oscilloscope up to these ports is important. If you strip the wires on the other end away from the board, make sure you protect them from accidentally contacting other component. [These plugs](https://github.com/jamespilgrim/Wire-end-Plug) can be used for that purpose.
5. Attach the 12V input and output wire pairs to the Dual MOS component’s screw terminals. It’s best NOT to tin the ends, as the terminals seem to make better contact that way. Add some plugs (e.g.,  [Ideal Industries 30-102 Power Plug](https://www.amazon.com/Ideal-Industries-30-102-Power-72427/dp/B01LYF1WV9/ref=sr_1_10?crid=35QTOVMFFT8MM&dib=eyJ2IjoiMSJ9.E2lbqLFKVG_hmleCqoMKymmILpx7xHQqbjKLlS_O8biic815YLA-1NACjC7tLtjM1exKso1Yz_65dH1SEbMni2IVp4idBpijPJhdQ_dqc_jCaEmkTbkPx0xoOe5u_wFYZDU-708_oXsrAmTNBXO-a2iAVH3lMZzeo9MZ6zFEO29IgxN_2m_0GrCkWTMORn0iifYao1peHnHenuPGSKCOTzvpolrumaoD-CkeZ5nvs29wAsiRDyq-Yq2K7tfmSzrQslG_ZpLzrx5-9GZQe55ktJDEK1bqKpEjHi-F_Lg1tuk.lAcQi1tp-upRzLoq8eleSXFSf1Vet_Xild25OSMBkYI&dib_tag=se&keywords=ideal+power+plug&qid=1732650183&sprefix=ideal+power+plug%2Caps%2C174&sr=8-10).) to the ends of the leads to make it easier to connect/disconnect them later to the strobe and the LED power supply. The use of different ends for input and output (e.g., one male, one female) may make it more difficult to later plug these in wrong (output to input)!
