# Hisense Mini-Split Protocol (A1 21)

## Message Format
```
A1 21 00 00 [mode] 10 [Hz] [?] 00 [indoor] [outdoor] 00 00 00 00 [chk]
         b4       b5  b6   b7    b9       b10                   b15
```

## Byte 4: Mode
- 0x0C = idle/off
- 0x0D = cooling (reversing valve de-energized)
- 0x1D = heating (reversing valve energized)
- Bit 4 (0x10) is the heat flag

## Byte 6: Compressor Hz
- Range: 12-47+ tested
- 0 = compressor off

## Byte 9: Indoor Fan Speed
- 0x00 = off
- 0x50 (80) = low
- 0x8A (138) = medium  
- 0xC0 (192) = high
- Continuous control

## Byte 10: Outdoor Fan Speed
- 0x50 (80) = low
- 0x64 (100) = medium
- 0x7D (125) = high
- Continuous control

## Byte 15: Checksum
Simple sum of bytes 0-14 & 0xFF

## Notes
- Reversing valve only switches when compressor is stopped (0 Hz)
- Fan speed changes take ~10-15 seconds to take effect
- Display still shows temps when we emulate DISP (reads A3 responses)

# A3 Responses (INV → DISP)

## A3 21 Format
```
A3 21 00 00 00 00 00 [b7] [outdoor_coil] [outdoor_air] [b10] [Hz_echo] [b12] [b13] 00 [chk]
```

### Byte Mappings
- byte 8: outdoor coil temp
- byte 9: outdoor air temp  
- byte 11: compressor Hz echo

## A3 22 Format
```
A3 22 00 00 [indoor_air] [discharge] [indoor_coil] 00 00 00 00 00 00 00 00 [chk]
```

### Byte Mappings
- byte 4: indoor air temp
- byte 5: discharge temp
- byte 6: indoor coil temp

## Temperature Encoding
Offsets vary per sensor (need span calibration):
- Indoor coil: ~25.5 offset
- Outdoor air: ~34 offset
- Indoor air: ~105 offset
- Discharge: ~101.5 offset
- Outdoor coil: ~124 offset

Formula: temp_C = raw_byte - offset

Note: Outdoor sensors may have encoding quirks (wrapping/conditional offsets) at certain ranges - needs more testing.
