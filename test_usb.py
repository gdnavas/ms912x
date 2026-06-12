#!/usr/bin/env python3
"""
Direct USB test for ms912x adapter.
Bypasses the kernel driver entirely to test if the hardware works.

Usage from a TTY (NOT from Hyprland):
  1. Switch to TTY: Ctrl+Alt+F2
  2. Login
  3. sudo modprobe -r ms912x
  4. python3 test_usb.py
  5. Check if the TV shows a blue screen
  6. Ctrl+C to stop
"""
import usb.core
import usb.util
import struct
import signal
import sys
import time

WIDTH = 1920
HEIGHT = 1080
VID = 0x534d
PID = 0x6021

def read(dev, address):
    data = [0] * 8
    data[0] = 0xb5
    data[1] = (address & 0xFF00) >> 8
    data[2] = address & 0xFF
    dev.ctrl_transfer(0x21, 0x09, 0x0300, 0, data)
    data = dev.ctrl_transfer(0xa1, 0x01, 0x0300, 0, 8)
    return data[3]

def write6(dev, address, six_bytes):
    data = [0xa6, address] + list(six_bytes)
    dev.ctrl_transfer(0x21, 0x09, 0x0300, 0, data)

def power_on(dev):
    write6(dev, 0x07, [1, 2, 0, 0, 0, 0])

def power_off(dev):
    write6(dev, 0x07, [0, 0, 0, 0, 0, 0])

def set_resolution(dev, width, height, mode_num, pix_fmt=0x2200):
    write6(dev, 0x04, [0, 0, 0, 0, 0, 0])
    read(dev, 0x30)
    read(dev, 0x33)
    read(dev, 0xc620)
    write6(dev, 0x03, [3, 0, 0, 0, 0, 0])

    data = [
        (width >> 8) & 0xFF, width & 0xFF,
        (height >> 8) & 0xFF, height & 0xFF,
        (pix_fmt >> 8) & 0xFF, pix_fmt & 0xFF,
    ]
    write6(dev, 0x01, data)

    data = [
        (mode_num >> 8) & 0xFF, mode_num & 0xFF,
        (width >> 8) & 0xFF, width & 0xFF,
        (height >> 8) & 0xFF, height & 0xFF,
    ]
    write6(dev, 0x02, data)

    write6(dev, 0x04, [1, 0, 0, 0, 0, 0])
    write6(dev, 0x05, [1, 0, 0, 0, 0, 0])

def make_blue_frame(width, height):
    """Create a full blue frame in UYVY format."""
    # UYVY: U0 Y0 V0 Y1 U2 Y2 V2 Y3 ...
    # Blue: Y=29, U=255, V=110
    line_size = width * 2  # UYVY = 2 bytes per pixel
    frame = bytearray(width * height * 2)

    for y in range(height):
        offset = y * line_size
        for x in range(0, width, 2):
            frame[offset + x * 2 + 0] = 255   # U
            frame[offset + x * 2 + 1] = 29     # Y
            frame[offset + x * 2 + 2] = 110    # V
            frame[offset + x * 2 + 3] = 29     # Y

    return bytes(frame)

def make_gradient_frame(width, height):
    """Create a color gradient frame in UYVY format."""
    line_size = width * 2
    frame = bytearray(width * height * 2)

    for y in range(height):
        offset = y * line_size
        for x in range(0, width, 2):
            r = (x * 255) // width
            g = (y * 255) // height
            b = ((width - x) * 255) // width

            # BT.601 RGB to YUV
            y1 = max(0, min(255, int(16 + 0.257 * r + 0.504 * g + 0.098 * b)))
            u1 = max(0, min(255, int(128 - 0.148 * r - 0.291 * g + 0.439 * b)))
            v1 = max(0, min(255, int(128 + 0.439 * r - 0.368 * g - 0.071 * b)))

            y2 = max(0, min(255, int(16 + 0.257 * r + 0.504 * g + 0.098 * b)))
            u2 = u1
            v2 = v1

            frame[offset + x * 2 + 0] = u1
            frame[offset + x * 2 + 1] = y1
            frame[offset + x * 2 + 2] = v1
            frame[offset + x * 2 + 3] = y2

    return bytes(frame)

def send_frame(dev, frame_data, x, y, width, height):
    """Send a frame to the device via USB bulk transfer."""
    # Frame update header (big endian)
    header = struct.pack('>HBHBH', 0xff00, x // 16, y, width // 16, height)

    end_marker = bytes([0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

    transfer_data = header + frame_data + end_marker

    # Find the bulk OUT endpoint (interface 3, endpoint 0x04)
    cfg = dev[0]
    intf = cfg[(3, 0)]
    ep = intf[0]

    print(f"Sending {len(transfer_data)} bytes to endpoint {ep.bEndpointAddress}...")
    try:
        ep.write(transfer_data, timeout=5000)
        print("Transfer OK")
    except usb.core.USBTimeoutError:
        print("Transfer timeout")
    except usb.core.USBError as e:
        print(f"USB error: {e}")

def main():
    print("=== ms912x Direct USB Test ===")
    print(f"Resolution: {WIDTH}x{HEIGHT}")
    print()

    dev = usb.core.find(idVendor=VID, idProduct=PID)
    if dev is None:
        print("ERROR: Device not found!")
        print("Make sure:")
        print("  1. The USB adapter is connected")
        print("  2. sudo modprobe -r ms912x (unload kernel driver)")
        sys.exit(1)

    print(f"Found device: {VID:04x}:{PID:04x}")

    # Detach kernel driver if active
    for i in range(4):
        try:
            if dev.is_kernel_driver_active(i):
                dev.detach_kernel_driver(i)
                print(f"  Detached kernel driver from interface {i}")
        except (usb.core.USBError, NotImplementedError):
            pass

    try:
        print(f"Device: {dev.manufacturer} {dev.product}")
    except:
        print("Device found (could not read name)")

    dev.set_configuration()
    print("USB configuration set")

    print("\n1. Powering on...")
    power_on(dev)
    time.sleep(0.5)

    print("2. Setting resolution...")
    set_resolution(dev, WIDTH, HEIGHT, 0x8100)
    time.sleep(0.5)

    print("3. Creating blue test frame...")
    frame = make_blue_frame(WIDTH, HEIGHT)

    print("4. Sending frame...")
    send_frame(dev, frame, 0, 0, WIDTH, HEIGHT)

    print("\nFrame sent! Check the TV screen.")
    print("Press Ctrl+C to exit and clean up.")

    def cleanup(sig, frame):
        print("\nCleaning up...")
        try:
            power_off(dev)
        except:
            pass
        sys.exit(0)

    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)

    # Keep sending frames periodically to maintain the image
    frame_count = 0
    while True:
        time.sleep(1)
        frame_count += 1
        if frame_count % 5 == 0:
            try:
                send_frame(dev, frame, 0, 0, WIDTH, HEIGHT)
            except:
                pass

if __name__ == "__main__":
    main()
