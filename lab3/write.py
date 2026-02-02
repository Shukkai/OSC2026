import serial
import os
import time
import sys
import argparse
from tqdm import tqdm

# --- Argument Parsing ---
parser = argparse.ArgumentParser(description='Send kernel payload via UART')
parser.add_argument('--port', type=str, default='/dev/ttyUSB0', 
                    help='Serial port (default: /dev/ttyUSB0 for Hardware)')
parser.add_argument('--baud', type=int, default=115200, 
                    help='Baud rate (default: 115200)')
parser.add_argument('--file', type=str, default='./build/payload.bin', 
                    help='Path to payload binary')

args = parser.parse_args()

# --- Setup ---
print(f"Opening {args.port} at {args.baud}...")
try:
    tty = serial.Serial(args.port, args.baud, timeout=1.0)
except serial.SerialException as e:
    print(f"Error opening port: {e}")
    sys.exit(1)

kernel_path = args.file
if not os.path.exists(kernel_path):
    print(f"Error: File {kernel_path} not found.")
    sys.exit(1)

file_stats = os.stat(kernel_path)
file_size = file_stats.st_size

print(f"Loading {kernel_path}...")
print(f"Size: {file_size} bytes")

# --- 1. Send Size ---
print("Sending size...")
tty.write(str(file_size).encode('utf-8'))
tty.write(b'\n')

# Delay to let board parse size and prepare buffer
time.sleep(0.1) 

# --- 2. Send Binary with TQDM ---
print("Sending kernel...")

with open(kernel_path, "rb") as fp:
    # Initialize tqdm progress bar
    with tqdm(total=file_size, unit='B', unit_scale=True, desc="Uploading") as pbar:
        byte = fp.read(1)
        while byte:
            tty.write(byte)
            byte = fp.read(1)
            
            # Update progress bar
            pbar.update(1)
            
            # Delay to prevent RX buffer overflow on board
            time.sleep(0.0005)

print("\nTransfer complete.")