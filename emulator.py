#!/usr/bin/env python3
"""
Hisense DISP Emulator

Emulates the display unit to control the AC inverter.
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

def checksum(msg):
    """Calculate checksum: sum of all bytes mod 256"""
    return sum(msg) & 0xFF

def make_a1_21(mode=0x00, fan=0x10):
    """A1 21 message - status/control"""
    msg = [0xA1, 0x21, 0x00, 0x00, mode, fan, 0x00, 0x00, 
           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    msg.append(checksum(msg))
    return msg

def make_a1_22(temp_set=0x64, temp1=0x5E, temp2=0x5E):
    """A1 22 message - temperatures"""
    msg = [0xA1, 0x22, 0x00, 0x00, temp_set, temp1, temp2, 0x00,
           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    msg.append(checksum(msg))
    return msg

def send_msg(ser, msg):
    """Send message as hex string command"""
    hex_str = 'T ' + ' '.join(f'{b:02X}' for b in msg) + '\n'
    ser.write(hex_str.encode())
    ser.flush()

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_rp2040()
    if not port:
        print("No RP2040 found")
        sys.exit(1)
    
    print(f"Connecting to {port}")
    ser = serial.Serial(port, 115200, timeout=0.1)
    time.sleep(0.5)
    ser.read(10000)  # clear buffer
    
    # Enable emulator mode
    print("Enabling emulator mode...")
    ser.write(b'e\n')
    time.sleep(0.2)
    resp = ser.read(1000).decode(errors='ignore')
    print(f"Response: {resp.strip()}")
    
    if 'EMU:1' not in resp:
        print("Failed to enable emulator mode")
        sys.exit(1)
    
    # Send startup sequence: zeros then FE
    print("Sending startup sequence...")
    zeros = [0x00] * 16
    for _ in range(20):  # send 320 zeros
        send_msg(ser, zeros)
        time.sleep(0.008 * 16)  # ~8ms per byte at 1200 baud
    
    # Send FE
    send_msg(ser, [0xFE])
    time.sleep(0.5)
    
    print("Starting message loop (Ctrl+C to stop)...")
    msg_num = 0
    
    try:
        while True:
            # Alternate A1 21 and A1 22
            if msg_num % 2 == 0:
                msg = make_a1_21()
            else:
                msg = make_a1_22()
            
            send_msg(ser, msg)
            print(f"TX: {' '.join(f'{b:02X}' for b in msg)}")
            
            # Read any responses
            time.sleep(0.1)
            data = ser.read(10000).decode(errors='ignore')
            for line in data.strip().split('\n'):
                if line.startswith('I:'):
                    print(f"  RX: {line}")
            
            msg_num += 1
            time.sleep(0.27)  # ~370ms total between messages
            
    except KeyboardInterrupt:
        print("\nStopping...")
    
    # Disable emulator mode
    ser.write(b'e\n')
    time.sleep(0.1)
    print("Emulator mode disabled")
    ser.close()

if __name__ == '__main__':
    main()
