---
title: PCB Assembly
layout: default
nav_order: 4
parent: Hardware
---

# Connector Board

The Connector Board is the central hub of your PiTrac. It handles connections between the Raspberry Pi computers and the strobe light while also providing regulated power for the IR LED array.

## Board Versions

### V2 - Dual Pi5 Connector Board (Current)

The V2 board is a complete redesign focused on simplification and cost reduction.

**What it does:**
- Connects two Pi units safely with shared power (no opto-couplers needed)
- Provides adjustable boost converter output (~15V to ~42V range)
- Stores energy in large caps for strobe pulses
- Adjustable LED current draw (supports various IR LED configurations)
- Hardware-enforced 10% duty cycle for thermal protection (dual 555 timers)
- Replaces several power supplies with a single +5V input

**What changed from V1:**

The original board was designed to connect two Raspberry Pi units with separate AC/DC supplies and opto-coupler isolation. This protected them from power switching issues that can kill a Pi. The V1 board works, but needed better integration to reduce system cost.

V2 moves to a single AC/DC supply, eliminating the need for opto-couplers by safely sharing power between Pi units. The integrated boost converter and LED driver circuit replace several external power supplies. All told, you go from multiple power supplies (including the LED driver) to a single +5V supply.

### V1 - Original Connector Board (Deprecated)

If you have a V1 board, it works fine. Just follow V1-specific instructions. If you're building new, use V2.

## Ordering the PCB

### Where to Order

Any decent PCB fabricator can make this board. Two popular options:

- **JLCPCB** - https://jlcpcb.com
- **PCBway** - https://www.pcbway.com

Both have easy file upload and offer assembly services.

{: .tip }
**Save Money:** Check Discord #pitrac-stuff-for-sale before ordering. Community members often have spare boards at cost.

### Fabrication Files

Everything the fab house needs is in one zip:

```
Hardware/Connector Board v2/Fabrication Files/Gerbers.zip
```

### Ordering Process

1. **Upload the Gerbers** - Upload Gerbers.zip to JLCPCB or PCBway

   ![Upload Gerber Files]({{ '/assets/images/hardware/upload-gerber-files.png' | relative_url }})
   ![Upload Gerber Files]({{ '/assets/images/hardware/upload-gerber-files-step-2.png' | relative_url }})
   
2. **Board specs** - Leave defaults, they're fine:
   - Material: FR4
   - Thickness: 1.6mm
   - Layers: 2
   - Copper: 1oz
   - Min trace/space: 5mil/5mil
   - Min hole: 0.25mm

3. **Surface Finish** - **Change to Lead-Free**

   Switch from "HASL with Lead" to "HASL Lead-Free"

   ![Surface Finish Selection]({{ '/assets/images/hardware/surface-finish.png' | relative_url }})

   Use lead-free solder for assembly, so get a lead-free board finish.

4. **Quantity** - Minimum order is usually 5 boards. You only need 1.

**Cost:** ~$40 shipped for 5 boards ($8/board) from PCBway

### Assembly Service (Optional)

Don't want to solder? Both JLCPCB and PCBway offer assembly services.

**Assembly files needed:**
```
Hardware/Connector Board v2/Assembly Files/
├── PiTrac Pi Connector Bill of Materials.csv
└── PiTrac Pi Connector Top Position.csv
```

Select Assembly Service:

![Assembly Service]({{ '/assets/images/hardware/assembly-service.png' | relative_url }})

The fab house sources components, solders everything, and ships you a completed board. Costs more, but saves significant time.

Once you have added the board with assembly to your cart you will need to provide the information they need for assembly.

![Shopping Cart]({{ '/assets/images/hardware/shopping-cart.png' | relative_url }})

**Top Position is the "Centroid" File**

![Upload Gerber Files]({{ '/assets/images/hardware/gerber-files.png' | relative_url }})

## Component Sourcing

### Bill of Materials

Complete BOM with Digikey links: [Parts List]({% link hardware/parts-list.md %})

**Cost:** ~$23 on Digikey (at time of writing)

### Out of Stock Parts?

If a part is unavailable, check the "Important Parameters" column in the parts list. Match those parameters when selecting alternates:

![Important Parameters]({{ '/assets/images/hardware/important-parameters.png' | relative_url }})

**Don't substitute these:**
- U1 - Boost controller (TI UCC2813DTR-3)
- L1 - Power inductor (Bourns RLB0914-330KL)
- Q2, Q4 - Power NMOS (Diodes DMT616MLSS-13)
- RV1, RV2 - Adjustment pots (Bourns 3362W-1-501LF)

**Can substitute if specs match:**
- Most capacitors and resistors
- Small transistors and diodes
- Logic ICs (if same function)

Unfortunately, some parts are challenging to find alternates for. Don't waste time hunting for alternatives to the critical components.

## Assembly

### Difficulty Assessment

**Skill Level:** Intermediate soldering required

The board was designed to be as approachable as possible, but it's still SMD work:

**What helps:**
- Oversized footprints (easier iron contact)
- All passives are 0603 or larger (still tiny, but not impossible)
- All SMD parts have external leads (no BGA or QFN nightmares)
- Works with or without flux

**What's challenging:**
- **Ground connections** - Most of the copper is ground, acting like a massive heatsink. Be patient. Let the heat soak in. The solder will stick.
- **Component size** - 0603 parts are small. Use a magnifying lamp.
- **Lead-free solder** - Higher temps, more patience than leaded.

![Component Size]({{ '/assets/images/hardware/0805.png' | relative_url }})

**Time estimate:**
- First build: 2-4 hours
- With experience: 1-2 hours

If you're a true novice at soldering, some of it will take time, especially ground connections. Be patient and let the heat soak in. You'll get the solder to stick.

### Assembly Tips

1. **Order matters:**
   - SMD components first (smallest to largest)
   - Through-hole components second
   - Connectors last

2. **For ground pads:**
   - Use 40W+ iron if available
   - Add extra solder to build thermal mass
   - Touch pad and component lead simultaneously
   - Wait 3-5 seconds for heat to soak

3. **Before power-up:**
   - Check for solder bridges with multimeter
   - Verify no shorts between power and ground
   - Double-check component orientation (ICs, diodes, polarized caps)

## Board Configuration

After assembly, adjust voltage and current before use.

### Test Points

**TP4** - Ground reference (use for all DC measurements)

**TP5** - Voltage output measurement
**TP11** - Current sense measurement

{: .warning }
**Silkscreen Error:** On unlabeled V1 boards, CC+/- and VIR+/- symbols are backwards. Meaning if you turn the knob towards +, it will actually decrease.

### Adjusting Current (RV2)

Sets maximum current to IR LED array.

1. Multimeter between **TP11** and **TP4 (GND)**
2. Adjust **RV2** potentiometer
3. Target: **100mV (0.1V)**
4. This equals 0.1V / 0.033Ω = **~3.03A**

### Adjusting Voltage (RV1)

Sets boost converter output voltage.

1. Multimeter between **TP5** and **TP4 (GND)**
2. Adjust **RV1** potentiometer
3. Target: **~36V** (typical, adjust for your LED requirements)
4. Don't worry about hitting it exactly - close is fine

### Thermal Protection

The dual 555 timer circuit forces a 10% duty cycle on the strobe line, preventing LED thermal runaway. The duty cycle limiting is adjustable by changing passive components per the schematic.

## Connections

### Power Input

- **J1:** 5V input from Meanwell LRS-75-5 (screw terminals)

### Power Output

- **J3/J4:** USB-C ports for Pi power (5V)

### Pi GPIO

- **J7:** 3-pin GPIO header for control signals (see assembly guide for pinout)

### LED Output

- **J2:** Regulated high-voltage output to IR LED array (screw terminals)
- Adjustable voltage via RV1
- Current limiting via RV2
- 10% max duty cycle (hardware enforced)

### Optional

- **J6 (USB-A):** Originally for LED strip, but Pi5 has USB 2.0 ports already - this is redundant

## Design Files

Full design documentation:
```
Hardware/Connector Board v2/
├── PiTrac Pi Connector Schematic.pdf
├── PiTrac Pi Connector.kicad_pcb
├── Design/                    (Excel calculations)
└── Fabrication Files/         (Production files)
```

KiCAD files are editable if you want to modify the design.

## Next Steps

Once assembled and configured:

1. Adjust voltage and current per above
2. Test with multimeter before connecting LEDs
3. Connect to Pi units and LED array
4. See [Assembly Guide]({% link hardware/assembly-guide.md %}) for system integration