#!/usr/bin/env python3
"""
Hisense Sniffer Capture

Reads from RP2040 sniffer, adds timestamps, outputs to stdout.

Usage:
    python capture.py                     # auto-detect port
    python capture.py /dev/tty.usbmodem*  # explicit port
    python capture.py | tee capture.log   # save to file
"""

import sys
import time
import serial
import serial.tools.list_ports

def find_rp2040():
    for p in serial.tools.list_ports.comports():
        if 'usbmodem' in p.device or 'ttyACM' in p.device:
            return p.device
    return None

def main():
    port = None
    for arg in sys.argv[1:]:
        if not arg.startswith('-'):
            port = arg
            break
    
    if not port:
        port = find_rp2040()
        if not port:
            print("ERROR: No RP2040 found", file=sys.stderr)
            sys.exit(1)
    
    print(f"# port: {port}", file=sys.stderr)
    
    ser = serial.Serial(port, 115200, timeout=0.1)
    time.sleep(0.3)
    ser.read(10000)  # clear buffer
    
    print("# capture started", file=sys.stderr)
    
    start = time.time()
    
    try:
        while True:
            line = ser.readline()
            if not line:
                continue
            
            try:
                line = line.decode().strip()
            except:
                continue
            
            if not line:
                continue
            
            ts = time.time() - start
            
            if line.startswith('D:') or line.startswith('I:'):
                # Data line - add timestamp
                print(f"{ts:.6f} {line}")
                sys.stdout.flush()
            elif line.startswith('HB'):
                # Heartbeat - skip or log to stderr
                pass
            else:
                # Other output
                print(f"# {line}", file=sys.stderr)
                
    except KeyboardInterrupt:
        print("\n# stopped", file=sys.stderr)
    finally:
        ser.close()

if __name__ == '__main__':
    main()
