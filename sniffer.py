#!/usr/bin/env python3
"""
Hisense Sniffer Control

Usage:
    python sniffer.py status          # print status
    python sniffer.py passthrough     # set passthrough mode
    python sniffer.py capture         # set capture mode (no forward)
    python sniffer.py emulator        # set emulator mode
    python sniffer.py send-inv XX XX  # send bytes to inverter
    python sniffer.py send-disp XX XX # send bytes to display
"""

import sys
import serial
import serial.tools.list_ports
import time

def find_rp2040():
    for p in serial.tools.list_ports.comports():
        if 'usbmodem' in p.device or 'ttyACM' in p.device:
            return p.device
    return None

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    
    cmd = sys.argv[1]
    
    port = find_rp2040()
    if not port:
        print("ERROR: No RP2040 found", file=sys.stderr)
        sys.exit(1)
    
    ser = serial.Serial(port, 115200, timeout=0.5)
    time.sleep(0.2)
    ser.read(10000)
    
    if cmd == 'status':
        ser.write(b'?\n')
        time.sleep(0.1)
        print(ser.read(1000).decode(errors='ignore').strip())
        
    elif cmd == 'passthrough':
        ser.write(b'p\n')
        time.sleep(0.1)
        print(ser.read(1000).decode(errors='ignore').strip())
        
    elif cmd == 'capture':
        ser.write(b'c\n')
        time.sleep(0.1)
        print(ser.read(1000).decode(errors='ignore').strip())
        
    elif cmd == 'emulator':
        ser.write(b'e\n')
        time.sleep(0.1)
        print(ser.read(1000).decode(errors='ignore').strip())
        
    elif cmd == 'send-inv':
        hex_bytes = ' '.join(sys.argv[2:])
        ser.write(f'I {hex_bytes}\n'.encode())
        time.sleep(0.1)
        print(ser.read(1000).decode(errors='ignore').strip())
        
    elif cmd == 'send-disp':
        hex_bytes = ' '.join(sys.argv[2:])
        ser.write(f'D {hex_bytes}\n'.encode())
        time.sleep(0.1)
        print(ser.read(1000).decode(errors='ignore').strip())
        
    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)
    
    ser.close()

if __name__ == '__main__':
    main()
