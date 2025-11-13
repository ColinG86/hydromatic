#!/usr/bin/env python3
"""Reset ESP32 device via DTR/RTS"""
import serial
import time

PORT = '/dev/ttyUSB0'

print("[RESET] Resetting device...")
ser = serial.Serial(PORT, 115200)
ser.setDTR(False)
ser.setRTS(True)
time.sleep(0.1)
ser.setRTS(False)
ser.close()
print("[RESET] Device reset complete")
