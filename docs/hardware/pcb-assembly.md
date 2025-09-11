---
title: PCB Assembly
layout: default
nav_order: 4
parent: Hardware
---

# Connector Board Overview

The PiTrac system requires a custom Connector Board - a small printed circuit board that handles connections between the Pi computer(s) and the strobe light as well as providing the power for the strobe.

## Manufacturing the PCB

### Design Files

The PCB design files are available in the main PiTrac repository under:
- `Hardware/Dual Pi5 Connector Board` directory

To simply get the fabrication files pack you can grab `Hardware/Dual Pi5 Connector Board/Fabrication Files/Gerbers.zip`

### Fabrication

**Before ordering your own boards it is worth checking our Discord under the "pitrac-stuff-for-sale" channel as there is usually a high chance of some spares floating around.**

- **Cost:** At time of writing PCBway works out to ~$40 shipped for 5 boards or $8/board.
- **Complexity:** Easy to build by all major consumer accessible board houses. FR4, 1.6mm finished thickness, 2-layer, 1oz outers, 6mil/6mil trace width and space, 0.25mm minimum hole size.
- **Quantity:** Only 1 is needed for a PiTrac

Popular PCB manufacturers include:
- JLCPCB
- PCBWay  
- OSHPark
- Any local PCB fabrication service

When placing your order, if HASL is chosen as the surface treatment, ensure it is LEAD-FREE.

## Component Assembly

### Required Components

BOM can be found in two locations:
`Hardware/Dual Pi5 Connector Board/Assembly Files` Contains the Excel sheet as designed in 9/2025
or
`docs/hardware/parts-list.md` Transcribed BOM as designed with direct links on Digikey for reference. Information provided for selecting alternates for possible as well.

### Assembly Tips

1. **Component orientation:** Pay careful attention to IC orientation and polarity

2. **Soldering order:** 
   - Install surface mount components first
   - Thru-hole should be second

3. **Testing:** Test continuity and check for shorts before connecting to Pi systems

## Connection Diagram

###Section under construction

Refer to the PCB wiring diagram in the [Assembly Guide]({% link hardware/assembly-guide.md %}) for proper connections to:
- Pi GPIO pins
- LED power supply
- Camera trigger signals
- Power input

## Troubleshooting

- **No strobe pulse:** Check power connections and MOS driver module
- **Camera not triggering:** Verify optocoupler installation and GPIO connections  
- **System instability:** Ensure proper grounding and power isolation

For detailed wiring instructions, see the [Assembly Guide]({% link hardware/assembly-guide.md %}).