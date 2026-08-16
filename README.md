# flexispot-esphome

ESPHome component for FlexiSpot standing desks (E7 Pro / E7 Pro Plus / LoctekMotion). Full programmatic control - presets, up/down, height sensor - via the desk's RJ45 serial interface.

Built for the **XIAO ESP32C6** with a bi-directional logic level shifter. Targets the E7 Pro's HS13M-1C0 controller, which has known issues with existing community integrations.

> **Status:** Working. Height sensor, preset commands (Stand/Sit/1/2), and manual Up/Down all tested on E7 Pro Plus + HS13M-1C0 + CB38M2L.

## Use Cases

- **Health break enforcement** - pair with a timer automation to auto-raise the desk after prolonged sitting
- **Scene integration** - raise to standing when "work mode" activates, lower for "meeting mode"
- **Sit/stand tracking** - log height over time to track daily standing ratio

## Compatibility

**Tested on:**
- FlexiSpot E7 Pro Plus (HS13M-1C0 keypad, CB38M2L control box)

**Should work with:**
- FlexiSpot E7, E7 Pro, E7Q, E5, E6, E8 and other LoctekMotion-based desks
- Any desk with the standard `9B ... 9D` packet protocol on 9600 baud RJ45

**ESP32 boards:** Designed for XIAO ESP32C6 but adaptable to any ESP32 variant. Adjust pin assignments in the YAML config. If using a 5V-tolerant board (some ESP32 DevKits), the level shifter may be optional, though it's recommended for reliability.

## What's Different

Existing projects ([iMicknl/LoctekMotion_IoT](https://github.com/iMicknl/LoctekMotion_IoT), forks) often report that E7 Pro desks read height but ignore movement commands. Three root causes:

1. **Wake timing** - Controller requires PIN 20 held HIGH for 1 full second, not 200ms.
2. **Poll/response protocol** - Controller sends `0x11` polls every ~40ms expecting `0x02` button-state replies. Fire-and-forget commands get ignored.
3. **Voltage levels** - The desk's logic level hasn't been publicly documented. If it runs at 5V, ESP32's 3.3V output would be marginal.

This component addresses #1 and #2 in software with proper wake sequencing and a poll-aware state machine. The reference design includes a level shifter for #3, though it may not be strictly required (see note below).

## Hardware

### Quick Start (No Soldering)

If you want to get this running as fast as possible, you don't need a soldering iron. Three parts, about 10 minutes of assembly:

| Part | Price | Source |
|------|-------|--------|
| Seeed Studio XIAO ESP32C6 (**pre-soldered** version) | ~$7 | [Amazon](https://www.amazon.com/s?k=XIAO+ESP32C6) / [Seeed Studio](https://www.seeedstudio.com) |
| Mini breadboard (170 tie points) | ~$1 | [Amazon](https://www.amazon.com/dp/B09YXQJMTG) |
| Cat6 ethernet patch cable (standard, **not slim/flat**) | ~$4 | [Amazon](https://www.amazon.com/dp/B00N2VISLW) |

**Total cost:** ~$12. No level shifter, no jumper wires, no tools beyond wire strippers.

> [!IMPORTANT]
> Get a **standard round Cat6** cable, not a slim or flat one. Cat6 uses 23 AWG solid-core conductors that push directly into breadboard holes. Slim/flat cables use thinner stranded wire that won't grip.

**Assembly:** Cut one end off the patch cable, strip ~2cm of jacket, and push the 5 needed wires directly into the breadboard rows next to the matching XIAO pins:

![Breadboard Wiring](docs/images/breadboard-wiring.png)

| Cat6 Wire (T568B) | RJ45 Pin | XIAO Pin | Signal |
|--------------------|----------|----------|--------|
| White-Brown | Pin 7 | GND | Ground |
| Brown | Pin 8 | 5V | Power (connect after testing with USB) |
| Green | Pin 6 | D6 / GPIO16 | Commands to desk (TX) |
| White-Blue | Pin 5 | D7 / GPIO17 | Height from desk (RX) |
| Blue | Pin 4 | D2 / GPIO2 | PIN 20 Wake |

**Unused wires:** White-Orange (Pin 1), Orange (Pin 2), White-Green (Pin 3) - cut short or tuck aside.

**Power:** During testing, leave the Brown (5V) wire disconnected and power the XIAO via USB-C. For permanent deployment, push the Brown wire into the 5V row. Both can be connected at the same time safely.

Plug the RJ45 end into the desk control box's **spare port** (not the one the keypad uses). That's it.

### Soldered Build (with Level Shifter)

For a more permanent setup, or if the breadboard version has signal issues, add a logic level shifter between the ESP and the desk:

| Part | Price | Source |
|------|-------|--------|
| Seeed Studio XIAO ESP32C6 | ~$7 | [Seeed Studio](https://www.seeedstudio.com) |
| SparkFun Logic Level Converter (BOB-12009) | ~$5 | [SparkFun](https://www.sparkfun.com/sparkfun-logic-level-converter-bi-directional.html) / Amazon |
| Short ethernet patch cable (any Cat5/5e/6) | ~$3 | Any retailer |

> [!TIP]
> **The level shifter may not be strictly required.** Community reports suggest several CB38M2L setups work fine with a 3.3V ESP32 wired directly, and at least one measurement of the data lines showed ~3V, not 5V. I included the shifter to eliminate voltage as a variable. At ~$5 there's no real downside, but the breadboard version above works without it.

**Tools needed:** Soldering iron, wire strippers

**Total cost:** ~$15

#### Wiring

![Wiring Diagram](docs/images/wiring-diagram.png)

```
XIAO ESP32C6 Level Shifter (BOB-12009) Desk RJ45 (T568B)
───────────── ─────────────────────── ─────────────────
3V3 ───────► LV (ref)
GND ───────► GND (LV) GND (HV) ◄─────── Pin 7 Wht-Brown GND
 HV (ref) ◄─────── Pin 8 Brown +5V
D6 / GPIO16 ───────► LV1 ─── ch1 ── HV1 ───────► Pin 6 Green Commands IN
D7 / GPIO17 ◄─────── LV2 ─── ch2 ── HV2 ◄─────── Pin 5 Wht-Blue Height OUT
D2 / GPIO2 ───────► LV3 ─── ch3 ── HV3 ───────► Pin 4 Blue PIN 20 Wake
5V (deploy) ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ► Pin 8 Brown +5V
```

#### Assembly

1. Cut one end off the patch cable, strip ~2cm of jacket, expose the 5 needed wires
2. Solder the 5 desk wires to the HV side of the level shifter (see table below)
3. Solder 5 short wires from the XIAO's castellated pads to the LV side
4. Connect XIAO 3V3 to the LV reference pin

About 30 minutes of soldering total, even if you're new to it.

<details>
<summary>📷 Build photos</summary>

**Patch cable to level shifter** - 5 wires from the cut cable soldered to the HV side, heat shrink over the splice:

![Cable to level shifter](docs/images/build-1-cable-to-shifter.jpg)

**Level shifter to XIAO** - LV side wires connecting the SparkFun board to the XIAO ESP32C6:

![Shifter to XIAO](docs/images/build-2-shifter-to-xiao.jpg)

**Full setup powered via USB-C** - ready for testing (connect 5V from desk for permanent deployment):

![Full setup](docs/images/build-3-full-setup.jpg)

</details>

| Level Shifter Pad | Desk Wire (HV side) | XIAO Pad (LV side) |
|--------------------|---------------------|---------------------|
| HV / LV (ref) | Pin 8 - Brown (+5V) | 3V3 |
| GND | Pin 7 - White-Brown | GND |
| HV1 / LV1 | Pin 6 - Green | D6 (GPIO16, TX) |
| HV2 / LV2 | Pin 5 - White-Blue | D7 (GPIO17, RX) |
| HV3 / LV3 | Pin 4 - Blue | D2 (GPIO2) |

## Installation

### Option A: Use as External Component (recommended)

Add this to your own ESPHome YAML — no need to clone the repo:

```yaml
external_components:
  - source: github://dimitri-vs/flexispot-esphome@main
    components: [ flexispot_desk ]
```

Then configure the component as shown in the [Configuration](#configuration) section below. See `esphome/office-desk.yaml` in this repo for a complete working example.

### Option B: Clone and Flash

```bash
pip install esphome
cd esphome/
cp secrets.yaml.example secrets.yaml
# Edit secrets.yaml with your WiFi credentials
esphome compile office-desk.yaml
esphome upload office-desk.yaml --device COM5  # or /dev/ttyACM0
```

### 2. Connect to Home Assistant

The device auto-discovers via mDNS. Go to **Settings > Devices > Add Integration > ESPHome** and enter the encryption key from your `secrets.yaml`.

### 3. Plug In

Insert the RJ45 cable into the desk control box's **spare port** (not the one the keypad uses).

## Configuration

### Height Unit

The height sensor reads the display digits directly, so it reports whatever unit your desk is set to. The component defaults to `cm`. If your desk displays inches, override it in your YAML:

```yaml
sensor:
  - platform: flexispot_desk
    name: "Desk Height"
    unit_of_measurement: "in"  # for desks set to inches
```

### Available Entities

| Entity | Type | Description |
|--------|------|-------------|
| Desk Height | Sensor | Current height from the 7-segment display |
| Stand | Button | Move to standing preset |
| Sit | Button | Move to sitting preset |
| Preset 1 | Button | Move to memory preset 1 |
| Preset 2 | Button | Move to memory preset 2 |
| Up | Button | Nudge up (`nudge_duration`, default 5 s) |
| Down | Button | Nudge down (`nudge_duration`, default 5 s) |
| Memory | Button | Enter memory/save mode |

Up/Down are short nudges, not continuous hold. The physical keypad always works for manual control and can override any programmatic command.

### Command Timing

How long a button is held on the bus is configurable. Both options are optional and default to
the values that were previously hardcoded, so existing configs are unaffected.

```yaml
flexispot_desk:
  id: my_desk
  uart_id: desk_uart
  wake_pin:
    number: GPIO4
    mode: OUTPUT
  nudge_duration: 400ms   # Up/Down hold time (default 5000ms)
  preset_hold: 1000ms     # Stand/Sit/Preset/Memory hold time (default 1000ms)
```

A 5-second nudge is a long way on a desk: measured on an E7 Plus (`CB38M2L(IB)-1` +
`HS13M-1C0`) at 25 mm/s, one tap of Up travelled **4.6 inches / 117 mm of commanded motion**.
Shorten `nudge_duration` for finer control.

### There is a floor on how small a nudge can be

⚠️ **Shortening `nudge_duration` has a hard limit, and it is lower than you would expect.** The control
box keeps driving after the last held-key frame, at close to full speed rather than decelerating — and on
the desk measured here **that overrun is a constant, not proportional to how long the key was held**:

| `nudge_duration` | Total travel, Up | Total travel, Down |
| --- | --- | --- |
| 5000 ms | +5.9 in (4.6 commanded + 1.3 overrun) | −5.0 in (4.6 commanded + 0.4 overrun) |
| 400 ms | **+1.1 to +1.4 in** (overrun is essentially all of it) | **−0.4 to −1.0 in** |

A twelvefold shorter command moved the desk roughly a fifth as far, not a twelfth — because at 400 ms the
commanded portion is near zero and the tail dominates. Setting 100 ms would very likely travel the same
distance as 400 ms.

**Practical guidance:** ~400 ms gives about an inch per tap, which is usable. Going lower buys nothing.
The overrun is also **asymmetric — up overruns roughly 3× more than down**, consistently across both
rounds. Presumed to be the control box's own soft-start/soft-stop ramp; not verified against vendor
documentation, and not confirmed on any control box other than `CB38M2L(IB)-1`.

This matters for anyone building a target-height control on top: a *constant* offset is far easier to
compensate than a proportional one, but no single "stop early by the offset" approach can land closer than
about an inch — a fine-correction pass is required.

Both values are printed at boot by `dump_config`.

## How It Works

The component implements a 5-state machine that emulates a keypad on the desk's spare RJ45 port:

1. **BOOT** - 10s delay for controller startup, then sends M command to get initial height
2. **IDLE** - Sends periodic "no buttons pressed" packets to maintain bus presence
3. **WAKING_LOW** - Pulls PIN 20 LOW for 100ms to create a rising edge
4. **WAKING_HIGH** - Holds PIN 20 HIGH for 1.1s (required by E7 Pro before accepting commands)
5. **ACTIVE** - Responds to controller's `0x11` polls with button-state packets encoding the requested command

Commands are only sent as responses to controller polls, not fire-and-forget. This matches the keypad scan conversation the controller expects. The physical keypad on the primary port continues to work normally.

## Protocol Reference

The desk uses a proprietary UART protocol at 9600 baud (8N1) over RJ45.

### Packet Format

```
[0x9B] [length] [type] [payload...] [CRC16] [0x9D]
```

### Packet Types (observed)

| Type | Direction | Purpose |
|------|-----------|---------|
| `0x11` | Controller -> Keypad | Status poll (~40ms interval) |
| `0x12` | Controller -> Keypad | Height display data (3 bytes, 7-segment encoded) |
| `0x02` | Keypad -> Controller | Button state response |

### Command Bytes

| Command | Full Packet |
|---------|-------------|
| No buttons | `9B 06 02 00 00 6C A1 9D` |
| Up | `9B 06 02 01 00 FC A0 9D` |
| Down | `9B 06 02 02 00 0C A0 9D` |
| Preset 1 | `9B 06 02 04 00 AC A3 9D` |
| Preset 2 | `9B 06 02 08 00 AC A6 9D` |
| Stand | `9B 06 02 10 00 AC AC 9D` |
| Sit | `9B 06 02 00 01 AC 60 9D` |
| Memory (M) | `9B 06 02 20 00 AC B8 9D` |

## Project Structure

```
flexispot-esphome/
├── components/
│   └── flexispot_desk/       # ESPHome external component
│       ├── __init__.py        # Component registration
│       ├── sensor.py          # Height sensor platform
│       ├── button.py          # Button platform (7 commands)
│       ├── flexispot_desk.h   # State machine, packets, constants
│       └── flexispot_desk.cpp # Implementation
├── esphome/
│   ├── office-desk.yaml       # Example config (XIAO ESP32C6)
│   ├── office-desk-debug.yaml # UART debug config (raw hex logging)
│   └── secrets.yaml.example
├── docs/
│   └── images/
│       ├── breadboard-wiring.png  # No-solder breadboard diagram
│       ├── wiring-diagram.html
│       └── wiring-diagram.png     # Soldered build with level shifter
└── README.md
```

## References

- [iMicknl/LoctekMotion_IoT](https://github.com/iMicknl/LoctekMotion_IoT) - Original reverse engineering project
- [NelsonBrandao/flexispot-e7-esphome](https://github.com/NelsonBrandao/flexispot-e7-esphome) - 3-state polling machine
- [takahashikenichi/flexispot-e7pro-nesson1](https://github.com/takahashikenichi/flexispot-e7pro-nesson1) - Confirmed E7 Pro + HS13M-1C0 control
- [Ideal Reality E7 Pro Analysis](https://ideal-reality.com) - Poll/response protocol documentation
- [PR #139](https://github.com/iMicknl/LoctekMotion_IoT/pull/139) - Wake-before-action fix for newer desks

## License

MIT
