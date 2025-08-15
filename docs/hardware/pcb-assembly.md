---
title: PCB Assembly
layout: default
nav_order: 4
parent: Hardware
---

# PCB Assembly

The PiTrac system requires a custom "Connector Board" - a small printed circuit board that handles connections between the Pi computers, cameras, and LED strobe system.

## Connector Board Overview

The Connector Board is the only custom electronic component in PiTrac. It:
- Provides signal isolation between systems
- Controls LED strobe timing
- Manages camera trigger signals
- Interfaces Pi computers with external hardware

## Manufacturing the PCB

### Design Files

The PCB design files are available in the main PiTrac repository under:
- `Hardware/Connector Board/` directory

### Fabrication

- **Cost:** A few dollars from most PCB manufacturers
- **Complexity:** Through-hole components only (no surface mount)
- **Quantity:** Only one board needed per PiTrac system

Popular PCB manufacturers include:
- JLCPCB
- PCBWay  
- OSH Park
- Any local PCB fabrication service

## Component Assembly

### Required Components

See the detailed component list in the [Parts List]({% link hardware/parts-list.md %}).

Key components include:
- H11L1 optocouplers (2x)
- 74HC04 hex inverter
- Dual MOS driver module
- Various resistors and connectors

### Assembly Tips

1. **Use sockets:** Install DIP sockets for the optocouplers and hex inverter to protect against heat damage during soldering

2. **Component orientation:** Pay careful attention to IC orientation and polarity

3. **Soldering order:** 
   - Install sockets first
   - Add resistors
   - Install headers and connectors
   - Insert ICs into sockets last

4. **Testing:** Test continuity and check for shorts before connecting to Pi systems

## Connection Diagram

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