# Hisense Mini-Split Protocol Documentation

## Overview

This document describes the communication protocol between the DISP (display/indoor unit controller) and INV (inverter/outdoor unit) on Hisense mini-split heat pumps.

**Physical Layer:**
- 1200 baud, 8N1 UART
- Ternary bus: 0V = INV tx, 8.5V = idle, 12V = DISP tx
- NFET level shifters on both sides
- 16-byte messages with simple sum checksum

## RP2040 Hardware Setup

### Pin Configuration
| Pin | Function | Direction | Notes |
|-----|----------|-----------|-------|
| GP0 | TX_DISP | Output | To DISP side (active high, no inversion) |
| GP1 | RX_DISP | Input | From DISP side (active high, no inversion) |
| GP4 | TX_INV | Output | To INV side (active low, inverted in PIO) |
| GP5 | RX_INV | Input | From INV side (active low, internal pullup, inverted in PIO) |

### Level Shifting
- DISP side: 12V logic, use voltage divider or level shifter to 3.3V
- INV side: 0V active, 8.5V idle - NFET open drain with pullup
- RP2040 handles inversion in PIO for INV side

### PHY Circuit (Ternary Bus Interface)

**System params:** 12V inverter pullup through 1.4kΩ, display biases to 8.6V, TX pulls to 0V

**RX_DISP** - read display output (GP1)
```
Signal: 8.6V (active) vs 12V (idle)
Output: 3.24V (HIGH) vs 0V (LOW)

       12V
        │
    [PFET source]
        │
SI_DISP─┤gate
        │
    [PFET drain]
        │
       27k
        ├───> output (to GPIO)
       10k
        │
       GND
```

**TX_INV** - write to inverter, emulate display (GP4)
```
Signal: 8.43V (active) vs 12V (idle)
Input: GPIO HIGH = pull to 8.6V, GPIO LOW = release

SI_INV──[NFET drain]
              │
           [NFET source]
              │
            3.3k
              │
             GND

GPIO──┬──[NFET gate]
      │
     10k
      │
     GND
```

**RX_INV** - read inverter output (GP5)
```
Signal: 0V (active) vs 8.6V/12V (idle)
Output: 3.3V vs 0V (ACTIVE LOW / INVERTED)

SI_INV──10k──┬──[NFET gate]
             │
            10k
             │
            GND

GPIO (internal pullup)──[NFET drain]
                             │
                        [NFET source]
                             │
                            GND
```

**TX_DISP** - write to display, emulate inverter (GP0)
```
Signal: 0V (active) vs 12V (idle)
Input: GPIO HIGH = pull to 0V, GPIO LOW = release

       12V
        │
       1.4k (external or use existing inverter pullup)
        │
SI_DISP─┴──[NFET drain]
                │
           [NFET source]
                │
               GND

GPIO──┬──[NFET gate]
      │
     10k
      │
     GND
```

**BOM:** 3x NFET + 1x PFET (or 4x NFET with different RX_DISP topology)

### PIO Allocation
**PIO0 (pio_pt):**
- SM0: Passthrough DISP→INV
- SM1: Passthrough INV→DISP  
- SM2: UART TX to INV (EMU mode)

**PIO1 (pio_uart):**
- SM0: UART RX from DISP (DMA drained)
- SM1: UART RX from INV (inverted)

### DMA
- Channel auto-claimed for DISP RX
- 256-entry ring buffer (1KB aligned)
- Hardware ring wrap at bit 10
- Continuous drain, polled by process_disp_dma()

## Message Types

### DISP → INV (A1 messages)

#### A1 21 - Control Message
```
Byte:  0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
     [A1] [21] [00] [00] [MODE][10] [Hz] [??] [??] [IN] [OUT][??] [??] [??] [??] [CHK]
```

| Byte | Name | Values |
|------|------|--------|
| 4 | Mode | 0x0C = idle, 0x0D = cooling, 0x1D = heating (bit 4 = heat flag) |
| 6 | Compressor Hz | 0x00 = off, 0x0C = 12Hz min, up to 0x2F+ = 47Hz+ |
| 7 | Unknown | Changing had no effect in testing |
| 8 | Unknown | Testing for slinger control |
| 9 | Indoor Fan Speed | 0x00 = off, 0x50 = low, 0x8A = medium, 0xC0 = high (continuous) |
| 10 | Outdoor Fan Speed | 0x50 = low, 0x64 = medium, 0x7D = high (continuous) |
| 11-14 | Unknown | Currently 0x00, testing for slinger |
| 15 | Checksum | sum(bytes 0-14) & 0xFF |

**Mode Switching:**
- MUST stop compressor (Hz = 0) before changing mode byte
- Reversing valve switches when mode changes between 0x0D and 0x1D
- Then restart compressor at desired Hz

#### A1 22 - Status Message
```
Byte:  0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
     [A1] [22] [00] [MODE][??] [??] [??] [00] [00] [00] [00] [00] [00] [00] [00] [CHK]
```

| Byte | Name | Values |
|------|------|--------|
| 3 | Mode Flag | 0x00 = off, 0x80 = on |
| 4-6 | Indoor temps | Sent by real DISP, we send fake values in EMU mode |

### INV → DISP (A3 messages)

#### A3 21 - INV Status
```
Byte:  0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
     [A3] [21] [00] [00] [00] [00] [00] [??] [??] [??] [??] [Hz] [??] [??] [00] [CHK]
```

| Byte | Name | Encoding |
|------|------|----------|
| 8 | Unknown | Did not respond to water bath tests |
| 9 | Unknown | Did not respond to water bath tests |
| 11 | Compressor Hz Echo | Direct value, matches commanded Hz |

#### A3 22 - INV Sensors
```
Byte:  0    1    2    3    4    5    6    7    8    9   10   11   12   13   14   15
     [A3] [22] [00] [00] [OA] [OC] [DIS][00] [00] [00] [00] [00] [00] [00] [00] [CHK]
```

| Byte | Name | Formula | Calibration |
|------|------|---------|-------------|
| 4 | Outdoor Air | temp = (byte - 81.5) / 1.966 | 3-point verified, errors < 0.1°C |
| 5 | Outdoor Coil | temp = (byte - 79) / 2 | 2-point verified |
| 6 | Discharge | temp = byte | 3-point verified (17-90°C), errors < 0.5°C, NO WRAP |

## Temperature Sensor Calibration

### Methodology
Water bath calibration using food thermometer as reference:
- Cold: ~15-20°C
- Room: ~25-30°C  
- Hot: ~45-90°C

### Results

**Indoor Air (D> A1 22 byte 5):**
| Actual °C | Byte | Calculated °C | Error |
|-----------|------|---------------|-------|
| 19.7 | 80 (0x50) | 20.0 | +0.3 |
| 45.4 | 131 (0x83) | 45.5 | +0.1 |

Formula: `temp_C = (byte - 40) / 2`

**Indoor Coil (D> A1 22 byte 6):**
Same encoding as indoor air: `temp_C = (byte - 40) / 2`

**Outdoor Air (A3 22 byte 4):**
| Actual °C | Byte | Calculated °C | Error |
|-----------|------|---------------|-------|
| 15.5 | 112 | 15.52 | +0.02 |
| 29.8 | 140 | 29.77 | -0.03 |
| 61.8 | 203 | 61.81 | +0.01 |

Formula: `temp_C = (byte - 81.48) / 1.966`

**Outdoor Coil (A3 22 byte 5):**
| Actual °C | Byte | Calculated °C | Error |
|-----------|------|---------------|-------|
| 17.4 | 114 | 17.4 | 0 |
| 58.3 | 196 | 58.3 | 0 |

Formula: `temp_C = (byte - 79) / 2`

**Discharge (A3 22 byte 6):**
| Actual °C | Byte | Calculated °C | Error |
|-----------|------|---------------|-------|
| 16.9 | 17 | 16.7 | -0.2 |
| 46.7 | 47 | 47.1 | +0.4 |
| 89.9 | 89 | 89.7 | -0.2 |

Formula: `temp_C ≈ byte` (direct reading)

**Note:** No wrapping issues observed up to 90°C. Previous decoder bugs in ESP32 RMT implementation caused apparent wrapping - RP2040 PIO gives clean data.

## Startup Sequence

1. Both sides send zeros for ~2.5s
2. DISP sends 0xFE, INV responds 0xFE (handshake)
3. DISP sends alternating A1 21 and A1 22 messages
4. INV responds with A3 21 and A3 22

## Response Times

- Compressor Hz: ~1-2 seconds
- Fan speeds: ~10-15 seconds
- Mode switch (reversing valve): requires compressor stop first

## Hardware Notes

### Indoor Sensors
Indoor air and indoor coil sensors are on the DISPLAY unit, sent via A1 22. 
In EMU mode, DMA captures D> messages so we can still read DISP-side temps.

### Indoor Fan RPM
**Not reported in protocol.** INV controls indoor fan speed via A1 21 byte 9 (command), but no RPM feedback exists in any A3 response. Tested by changing fan speed and watching all A3 bytes - no field changed.

Indoor fan is closed-loop controlled internally by INV, so commanded speed ≈ actual speed. Can verify with physical measurement once to get scaling factor if needed.

### Water Slinger
Separate motor, control byte not yet identified. Testing bytes 8, 11-14 in A1 21.

### Float Switch
E5 error when triggered. Location: A1 22 byte 3, bit 7 (1=normal, 0=water high).

## Lessons Learned

1. **ESP32 RMT is not suitable** for reliable UART sniffing - had persistent decode errors
2. **RP2040 PIO is rock solid** - clean data, no decode issues
3. **Display shows temps while we control INV** - it reads A3 responses off the bus, giving free monitoring
4. **All INV temp sensors are linear** - just different gain/offset per sensor
5. **Discharge sensor is direct reading** - no scaling needed
6. **DMA essential for EMU mode** - polling FIFO during TX/RX blocking loops causes overflow

## USB Commands (115200 baud)

| Command | Description |
|---------|-------------|
| `?` | Print status: version, mode, run state, slot A/B contents |
| `M` | Toggle mode: PT (passthrough) ↔ EMU (emulator) |
| `X` | Stop emulator (stays in EMU mode but stops TX) |
| `R` | Restart emulator |
| `!` | Reboot to USB bootloader (for flashing) |
| `A xx xx...` | Set slot A (16 hex bytes, auto-checksum if 15 provided) |
| `B xx xx...` | Set slot B (16 hex bytes, auto-checksum if 15 provided) |

### EMU Mode Operation
1. Send `M` to enter EMU mode (disables passthrough, waits for zeros)
2. When INV sends zeros (power cycled or lost DISP), emulator auto-starts
3. Alternates sending slot A (A1 21) and slot B (A1 22)
4. Receives and prints INV responses (I> prefix)
5. DMA continues capturing DISP messages (D> prefix) for monitoring

## Float Switches

Located in A3 21 byte 3 (INV reports to DISP):

| Bit | Value | Meaning |
|-----|-------|---------|
| 3 | 0x08 | Float switch level 1 triggered |
| 4 | 0x10 | Float switch level 2 triggered |
| - | 0x00 | Both switches normal (dry) |

## Cooling Mode Specifics

In A1 21 byte 5:
- 0x10 = heating mode setting
- 0x20 = cooling mode idle
- 0x24 = cooling mode with slinger (bit 2 = 0x04 may be slinger enable)
