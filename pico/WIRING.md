# Pico wiring

## BOM

- Raspberry Pi 5 (existing PiTrac install)
- Raspberry Pi Pico W
- SPH0645 I2S MEMS mic (Adafruit 3421)
- Micro-USB cable, data-capable
- ~22 jumper wires
- Existing V3 connector board + Innomaker camera adapter

## V3 Rev1 strobe gotcha

The V3 silkscreen says "MOSI is STROBE". That is wrong on Rev1. R20 + D3 are DNP, so SPI1 MOSI never reaches the gate driver. Strobe is driven via the `DIAG` net through D4.

- Pico GP13 goes to V3 **DIAG** (strobe out).
- Pi GPIO 20 stays on V3 **MOSI** (DAC SPI1, unchanged).

Verify on your unit before wiring: check that R20 and D3 are unpopulated. If either is populated, your board diverges from Rev1; trace continuity before energizing.

## Pin map

Pin numbers are physical (header position counted from the silkscreened "1"). GPIO numbers in parentheses.

| # | From | To | Function |
|---|------|------|----|
| 1  | Pi pin 1 (3V3)        | V3 V3P3                      | V3 power |
| 2  | Pi pin 39 (GND)       | V3 GND                       | V3 ground |
| 3  | Pi pin 20 (GND)       | Innomaker trigger bottom     | camera trigger ground |
| 4  | Pi USB-A              | Pico micro-USB               | Pico power + serial |
| 5  | Pi pin 6 (GND)        | Pico pin 38 (GND)            | ground bond (optional) |
| 6  | Pi pin 12 (GPIO 18)   | V3 CS0                       | SPI1 DAC select |
| 7  | Pi pin 11 (GPIO 17)   | V3 CS1                       | SPI1 ADC select |
| 8  | Pi pin 38 (GPIO 20)   | V3 MOSI                      | SPI1 DAC data in |
| 9  | Pi pin 35 (GPIO 19)   | V3 MISO                      | SPI1 ADC data out |
| 10 | Pi pin 40 (GPIO 21)   | V3 CLK                       | SPI1 clock |
| 11 | Pico pin 17 (GP13)    | V3 DIAG                      | strobe out (NOT MOSI) |
| 12 | Pico pin 20 (GP15)    | Innomaker trigger top        | cam2 external trigger |
| 13 | Pico pin 36 (3V3 OUT) | SPH0645 VIN                  | mic power |
| 14 | Pico pin 38 (GND)     | SPH0645 GND                  | mic ground |
| 15 | Pico pin 13 (GND)     | SPH0645 SEL                  | mic channel = left |
| 16 | Pico pin 14 (GP10)    | SPH0645 SCK / BCLK           | I2S clock |
| 17 | Pico pin 15 (GP11)    | SPH0645 WS / LRC             | I2S word select |
| 18 | Pico pin 16 (GP12)    | SPH0645 SD / DOUT            | I2S data |
| 19 | Pi pin 37 (GPIO 26)   | Pico pin 12 (GP9)            | FIRE_IN fast trigger |
| 20 | Pico pin 10 (GP7)     | Pi pin 13 (GPIO 27)          | IRQ_OUT strike notice |
| 21 | Pico pin 11 (GP8)     | Pi pin 15 (GPIO 22)          | HEARTBEAT_OUT liveness |
| 22 | V3 TP4                | Pico pin 31 (GP26 / ADC0)    | LED current sense for FIRE_PEAK |

## Migration from a Pi-only build

| Was                                  | Now                                      |
|--------------------------------------|------------------------------------------|
| Pi GPIO 10 → V3 DIAG (strobe)        | Pico GP13 → V3 DIAG                      |
| Pi GPIO 25 → Innomaker trigger top   | Pico GP15 → Innomaker trigger top        |
| Pi GPIO 20 → V3 MOSI (DAC SPI1)      | unchanged                                |

Stop the service before swapping: `sudo systemctl stop pitrac`. Restart when done.

Remove the old Pi-side wires fully. Two drivers on one net is bus contention.

## Verify

```bash
ls /dev/ttyACM*                          # /dev/ttyACM0
lsusb -v -d 2e8a: 2>/dev/null | grep iProduct   # "PiTrac Pico Strobe"
echo STATUS > /dev/ttyACM0               # via picocom or similar
```

Expected `SELFTEST` line shape:

```
SELFTEST vsys_mv=0 vbus=1 mic_rms=12 armed=0 fw=0.5.0
```

- `vsys_mv=0` on Pico W is normal (VSYS sense path is not wired).
- `vbus=0` while the Pico is replying = hardware fault, replace the Pico.
- `mic_rms` should be a small positive number. Huge or negative = recheck wires 15-18 and confirm SEL is on GND.

## Troubleshooting

- **LED solid ON.** Firmware halted mid-init. Capture boot `LOG` lines over `/dev/ttyACM0`.
- **LED solid OFF.** Firmware never reached LED init. Usually a `PICO_BOARD` mismatch. `picotool info -f` to confirm.
- **No `/dev/ttyACM0`.** Bad cable (many micro-B are power-only) or hung firmware. Reflash by holding BOOTSEL while plugging in and dropping the `.uf2` on the `RPI-RP2` volume.
- **Strobe doesn't fire on `FIRE`.** Wire 11 must land on V3 **DIAG**, not MOSI. Trace continuity from Pico GP13 to the MCP1407 input.
- **DAC calibration fails.** Wire 8 (Pi GPIO 20 → V3 MOSI) must stay connected. The old wiring guide called it vestigial; it isn't.
- **Camera doesn't trigger.** Wire 12 on Innomaker top pin. Confirm the old Pi GPIO 25 wire is fully detached.
