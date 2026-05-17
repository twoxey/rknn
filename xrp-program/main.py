from XRPLib.encoded_motor import EncodedMotor
from machine import UART, Pin
import time
# from XrpBLE import BleReceiver
from micropython import const
import struct

_DRIVE_STOP      = const(0)
_DRIVE_FORWARD   = const(1)
_DRIVE_SIDELEFT  = const(2)
_DRIVE_SIDERIGHT = const(3)
_DRIVE_TURNLEFT  = const(4)
_DRIVE_TURNRIGHT = const(5)
_DRIVE_BACKWARD  = const(6)
_DRIVE_HIGHSPEED = const(7)
_DRIVE_LOWSPEED  = const(8)

motorFL = EncodedMotor.get_default_encoded_motor(index=1)
motorFR = EncodedMotor.get_default_encoded_motor(index=2)
motorBL = EncodedMotor.get_default_encoded_motor(index=3)
motorBR = EncodedMotor.get_default_encoded_motor(index=4)

output_effort = -1.0

def set_effort(fl: float, fr: float, bl: float, br: float):
    motorFL.set_effort(fl)
    motorFR.set_effort(fr)
    motorBL.set_effort(bl)
    motorBR.set_effort(br)

def on_data_write(data: bytes):
    global output_effort

    (drive_command,) = struct.unpack("B", data)
    print(drive_command)

    if   drive_command == _DRIVE_FORWARD:   set_effort( output_effort,  output_effort,  output_effort,  output_effort)
    elif drive_command == _DRIVE_SIDELEFT:  set_effort( output_effort, -output_effort, -output_effort,  output_effort)
    elif drive_command == _DRIVE_SIDERIGHT: set_effort(-output_effort,  output_effort,  output_effort, -output_effort)
    elif drive_command == _DRIVE_TURNLEFT:  set_effort( output_effort, -output_effort,  output_effort, -output_effort)
    elif drive_command == _DRIVE_TURNRIGHT: set_effort(-output_effort,  output_effort, -output_effort,  output_effort)
    elif drive_command == _DRIVE_BACKWARD:  set_effort(-output_effort, -output_effort, -output_effort, -output_effort)
    elif drive_command == _DRIVE_HIGHSPEED: output_effort = -1.0
    elif drive_command == _DRIVE_LOWSPEED:  output_effort = -0.6
    else:                                   set_effort(0, 0, 0, 0) # drive_command == _DRIVE_STOP

# ble_receiver = BleReceiver("XrpDrive", "a0ba6193-d014-4eee-aa99-13c6b3b49711", on_data_write)

uart0 = UART(0, baudrate=9600, tx=Pin(16), rx=Pin(17), timeout=40)
print("Uart started")
last_ms = 0
while True:
    if last_ms + 20 <= time.ticks_ms():
        if uart0.any():
            data = uart0.read(1)
            on_data_write(data)
        last_ms = time.ticks_ms()
